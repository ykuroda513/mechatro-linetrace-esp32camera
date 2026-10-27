// =============================================================
//  linetrace.ino  カメラ式ライントレースロボット (XIAO ESP32S3 Sense)
//
//  処理の流れ (1フレームごと)
//    カメラ(160x120 グレースケール)
//      → line::Detector   … 3つの帯でラインの位置を求める
//      → ctrl::LineController … PD制御で左右の速度を決める
//      → Motor (DRV8835 IN/IN) … PWMで駆動
//
//  操作
//    - BOOTボタン: 走行開始 / 停止
//    - Wi-Fi "LineTrace-XXXX" に接続して http://192.168.4.1/ を開くと
//      画像確認・パラメータ調整・START/STOP ができます
// =============================================================
#include <Arduino.h>
#include <esp_system.h>
#include "config.h"
#include "camera.h"
#include "motor.h"
#include "encoder.h"
#include "drive.h"
#include "line_detector.h"
#include "controller.h"
#include "params.h"
#include "web.h"

static line::Detector detector;
static ctrl::LineController controller;

static void ledSet(bool on) { digitalWrite(PIN_USER_LED, on ? LOW : HIGH); }

// パラメータを検出器・制御器・カメラへ反映
static void applyParams(const Params& p) {
  line::Config dc = detector.config();
  dc.darkLine = p.darkLine >= 0.5f;
  dc.minContrast = (int)p.minContrast;
  detector.setConfig(dc);

  ctrl::Gains g = controller.gains();
  g.kp = p.kp;
  g.kd = p.kd;
  g.baseSpeed = p.baseSpeed;
  g.maxSpeed = p.maxSpeed;
  g.curveSlow = p.curveSlow;
  g.lookahead = p.lookahead;
  g.lostTurn = p.lostTurn;
  controller.setGains(g);

  if (shared::consumeCameraDirty())
    cameraSetExposure(p.autoExp >= 0.5f, (int)p.aeLevel, (int)p.exposure, (int)p.gain,
                      (int)p.brightness, (int)p.contrast);
}

// BOOTボタンで開始/停止 (押した瞬間を検出)
static void pollButton() {
  static bool prev = true;
  static uint32_t lastChange = 0;
  bool now = digitalRead(PIN_START_BUTTON);
  if (now != prev && millis() - lastChange > 50) {
    lastChange = millis();
    if (!now) {
      if (shared::running() || drive::state().testing) drive::stopAll();
      else shared::setRunning(true);
    }
    prev = now;
  }
}

// 前回リセットした理由 (電源不足やクラッシュの切り分け用)
static const char* resetReasonStr(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "POWERON (電源投入)";
    case ESP_RST_SW:       return "SW (ソフトウェアリセット)";
    case ESP_RST_PANIC:    return "PANIC (プログラムが異常終了)";
    case ESP_RST_INT_WDT:  return "INT_WDT (割り込みウォッチドッグ)";
    case ESP_RST_TASK_WDT: return "TASK_WDT (タスクウォッチドッグ)";
    case ESP_RST_WDT:      return "WDT";
    case ESP_RST_BROWNOUT: return "BROWNOUT (電源電圧低下!)";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    case ESP_RST_EXT:      return "EXT (リセットピン)";
    default:               return "OTHER";
  }
}

void setup() {
  Serial.begin(115200);
  // USBシリアルの再接続を最大2秒待つ (起動直後のログを取りこぼさないため)
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000;) delay(10);
  Serial.printf("\n[main] boot. reset reason: %s\n", resetReasonStr(esp_reset_reason()));
  pinMode(PIN_USER_LED, OUTPUT);
  ledSet(false);
  pinMode(PIN_START_BUTTON, INPUT_PULLUP);

  motorsBegin();          // 最初にモータを止めた状態にする (MODE=LOW: IN/IN)
  encodersBegin();
  analogSetPinAttenuation(PIN_VM_SENSE, ADC_11db);
  if (!psramFound())
    Serial.println("[main] PSRAM無効のまま動作します (推奨: ツール > PSRAM > OPI PSRAM)");
  shared::begin();
  drive::begin();         // モータ制御タスク (100Hz) 開始。パラメータを使うので shared::begin の後
  webBegin();             // カメラが壊れていてもページは開けるよう先に起動
  shared::setCameraOk(cameraBegin());
  if (!shared::cameraOk()) Serial.println("[main] camera init failed (ケーブル/コネクタを確認)");
  Serial.println("[main] ready. BOOTボタンで走行開始");
}

void loop() {
  static uint32_t prevUs = micros();
  static uint32_t lastGoodMs = millis();
  static uint32_t lastSeenMs = millis();
  static uint32_t frameNo = 0;
  static float fps = 0;
  static bool wasRunning = false;

  if (!shared::cameraOk()) {   // カメラなし: 停止したまま LED をゆっくり点滅
    drive::stopAll();
    ledSet((millis() / 500) % 2);
    delay(20);
    return;
  }

  pollButton();
  applyParams(shared::getParams());

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    drive::setLine(false, 0, 0);
    Serial.println("[main] frame grab failed");
    delay(10);
    return;
  }

  int w = 0, h = 0;
  const uint8_t* img = cameraGray(fb, &w, &h);
  if (!img) {                 // 欠けたフレームは捨てる (制御は前回のまま)
    static uint32_t badCount = 0;
    if (++badCount % 30 == 1)
      Serial.printf("[main] bad frame len=%u (%ux%u) x%lu\n", (unsigned)fb->len,
                    (unsigned)fb->width, (unsigned)fb->height, (unsigned long)badCount);
    esp_camera_fb_return(fb);
    if (millis() - lastGoodMs > 300) drive::setLine(false, 0, 0);   // 欠けが続いたら止める
    return;
  }
  lastGoodMs = millis();
  uint32_t nowUs = micros();
  float dt = (nowUs - prevUs) * 1e-6f;
  prevUs = nowUs;
  if (dt > 0) fps = 0.9f * fps + 0.1f * (1.0f / dt);

  line::Result det = detector.process(img, w, h);
  shared::putFrame(img, w, h);
  // 輝度の平均と最大 (4画素おき)。平均が250近くなら白飛び
  uint32_t lumaSum = 0, lumaN = 0;
  uint8_t lumaMax = 0;
  for (int i = 0; i < w * h; i += 4) {
    lumaSum += img[i];
    lumaN++;
    if (img[i] > lumaMax) lumaMax = img[i];
  }
  const int lumaMean = lumaN ? lumaSum / lumaN : 0;
  esp_camera_fb_return(fb);   // これ以降 img は使えない

  bool run = shared::running();
  if (run && !wasRunning) {           // 走行開始時に内部状態をリセット
    controller.reset();
    lastSeenMs = millis();
  }
  wasRunning = run;

  ctrl::Output out = controller.update(det, dt);
  if (!out.lost) lastSeenMs = millis();

  if (run && millis() - lastSeenMs > LOST_TIMEOUT_MS) {
    Serial.println("[main] line lost -> stop");
    shared::setRunning(false);
    run = false;
  }

  // 左右の目標速度 (最高速度比) を駆動タスクへ。速度FBは drive.cpp で行う
  drive::setLine(run, out.left, out.right);

  // LED: 停止中=消灯, 走行中=点灯, 見失い中=点滅
  ledSet(run && (!out.lost || (millis() / 100) % 2));

  Telemetry t;
  t.lumaMean = lumaMean;
  t.lumaMax = lumaMax;
  const drive::State ds = drive::state();
  t.spdL = ds.spdL;
  t.spdR = ds.spdR;
  t.encL = ds.encL;
  t.encR = ds.encR;
  t.vm = analogReadMilliVolts(PIN_VM_SENSE) * 1e-3f * VM_SENSE_RATIO;
  t.det = det;
  t.out = out;
  t.fps = fps;
  t.frameNo = ++frameNo;
  shared::putTelemetry(t);
}
