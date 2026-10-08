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
#include "config.h"
#include "camera.h"
#include "motor.h"
#include "encoder.h"
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

  if (shared::consumeCameraDirty()) cameraSetExposure((int)p.exposure, (int)p.gain);
}

// BOOTボタンで開始/停止 (押した瞬間を検出)
static void pollButton() {
  static bool prev = true;
  static uint32_t lastChange = 0;
  bool now = digitalRead(PIN_START_BUTTON);
  if (now != prev && millis() - lastChange > 50) {
    lastChange = millis();
    if (!now) shared::setRunning(!shared::running());
    prev = now;
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_USER_LED, OUTPUT);
  ledSet(false);
  pinMode(PIN_START_BUTTON, INPUT_PULLUP);

  motorsBegin();          // 最初にモータを止めた状態にする (MODE=LOW: IN/IN)
  encodersBegin();
  analogSetPinAttenuation(PIN_VM_SENSE, ADC_11db);
  if (!psramFound())
    Serial.println("[main] PSRAM無効のまま動作します (推奨: ツール > PSRAM > OPI PSRAM)");
  shared::begin();
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
    motorsBrake();
    shared::setRunning(false);
    ledSet((millis() / 500) % 2);
    delay(20);
    return;
  }

  pollButton();
  applyParams(shared::getParams());

  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) {
    motorsBrake();
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
    if (millis() - lastGoodMs > 300) motorsBrake();   // 欠けが続いたら止める
    return;
  }
  lastGoodMs = millis();
  uint32_t nowUs = micros();
  float dt = (nowUs - prevUs) * 1e-6f;
  prevUs = nowUs;
  if (dt > 0) fps = 0.9f * fps + 0.1f * (1.0f / dt);

  line::Result det = detector.process(img, w, h);
  shared::putFrame(img, w, h);
  esp_camera_fb_return(fb);

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

  if (run) {
    motorsSet(out.left, out.right);
    encL.setDirection(out.left);
    encR.setDirection(out.right);
  }
  else     motorsBrake();

  // LED: 停止中=消灯, 走行中=点灯, 見失い中=点滅
  ledSet(run && (!out.lost || (millis() / 100) % 2));

  Telemetry t;
  t.spdL = encL.update(dt);
  t.spdR = encR.update(dt);
  t.encL = encL.count();
  t.encR = encR.count();
  t.vm = analogReadMilliVolts(PIN_VM_SENSE) * 1e-3f * VM_SENSE_RATIO;
  t.det = det;
  t.out = out;
  t.fps = fps;
  t.frameNo = ++frameNo;
  shared::putTelemetry(t);
}
