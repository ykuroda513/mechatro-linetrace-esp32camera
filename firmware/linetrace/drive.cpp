// =============================================================
//  drive.cpp
//
//  目標速度 r [mm/s]、実測 y [mm/s]、最高速度 vmax [mm/s] (duty 100% 時, 左右別)
//  左右のモータは個体差があるので、vmax も左右別々に持つ
//    速度FB OFF:  duty = r / vmax
//    速度FB ON :  duty = SpeedPI(r / vmax, y / vmax)
//                      = FF×r/vmax + Kp×(r-y)/vmax + ∫Ki×(r-y)/vmax dt
// =============================================================
#include "drive.h"
#include <Arduino.h>
#include <stdio.h>
#include "config.h"
#include "encoder.h"
#include "motor.h"
#include "params.h"
#include "speed_ctrl.h"

namespace drive {
namespace {

portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// --- 指令 (mux で保護) ---
enum Mode { M_IDLE, M_LINE, M_TEST, M_CALIB };
bool lineRun = false;
float lineL = 0, lineR = 0;
bool testOn = false;
float testL = 0, testR = 0;
FbMode testFb = FB_PARAM;
uint32_t testUntil = 0;
uint32_t testGoal = 0;        // 回転数指定テスト: 目標カウント (0 = なし)
uint32_t testStartL = 0, testStartR = 0;
bool calibOn = false;
uint32_t calibStart = 0;
State st;

constexpr uint32_t CALIB_MS = 1500;       // 計測の全体時間
constexpr uint32_t CALIB_AVG_FROM = 800;  // この時刻以降 (加速後) の平均をとる

spd::SpeedPI piL, piR;

// --- ログ (100Hz, 3秒分) ---
constexpr int LOG_N = 300;
struct Sample { int16_t tl, tr, sl, sr, dl, dr; };   // 速度[mm/s], duty×1000
Sample logBuf[LOG_N];
int logHead = 0, logCount = 0;

int16_t clamp16(float x) {
  if (x > 32767) x = 32767;
  if (x < -32768) x = -32768;
  return (int16_t)x;
}

void clearLog() { logHead = 0; logCount = 0; }

void controlTask(void*) {
  const TickType_t period = pdMS_TO_TICKS(1000 / CONTROL_HZ);
  const float dt = 1.0f / CONTROL_HZ;
  TickType_t last = xTaskGetTickCount();
  Mode prevMode = M_IDLE;
  float dirL = 1, dirR = 1;   // 直前のモータ指令の向き
  double calSumL = 0, calSumR = 0;
  int calN = 0;

  for (;;) {
    vTaskDelayUntil(&last, period);
    const uint32_t now = millis();
    const Params p = shared::getParams();
    const float vmL = p.vmaxL > 1.0f ? p.vmaxL : 1.0f;
    const float vmR = p.vmaxR > 1.0f ? p.vmaxR : 1.0f;

    // --- 指令の取り出し ---
    Mode mode;
    float tl = 0, tr = 0;
    bool fb;
    uint32_t calibElapsed = 0;
    portENTER_CRITICAL(&mux);
    if (testOn && (int32_t)(now - testUntil) >= 0) testOn = false;   // テスト時間切れ
    if (calibOn) {
      mode = M_CALIB;
      calibElapsed = now - calibStart;
    } else if (testOn) {
      mode = M_TEST;
      tl = testL; tr = testR;
    } else if (lineRun) {
      mode = M_LINE;
      tl = lineL; tr = lineR;
    } else {
      mode = M_IDLE;
    }
    const FbMode tfb = testFb;
    const uint32_t goal = testGoal, startL = testStartL, startR = testStartR;
    portEXIT_CRITICAL(&mux);

    // 回転数指定テスト: 目標カウントに達した車輪は止める
    bool doneL = false, doneR = false;
    if (mode == M_TEST && goal > 0) {
      doneL = encL.count() - startL >= goal;
      doneR = encR.count() - startR >= goal;
      if (doneL) tl = 0;
      if (doneR) tr = 0;
      if (doneL && doneR) {
        portENTER_CRITICAL(&mux);
        testOn = false;
        portEXIT_CRITICAL(&mux);
      }
    }

    if (mode == M_TEST && tfb != FB_PARAM) fb = (tfb == FB_ON);
    else fb = p.spdFb >= 0.5f;
    if (mode == M_CALIB) { fb = false; tl = vmL; tr = vmR; }

    spd::Gains g;
    g.kp = p.spdKp;
    g.ki = p.spdKi;
    g.ff = p.spdFf;
    piL.setGains(g);
    piR.setGains(g);

    // --- 速度計測 [mm/s] ---
    // エンコーダは1相で向きが分からないので、
    // 「直前にモータに出した向きに回っている」とみなして符号を付ける
    const float yl = dirL * encL.update(dt) * MM_PER_COUNT;
    const float yr = dirR * encR.update(dt) * MM_PER_COUNT;

    // --- 出力 ---
    float dl = 0, dr = 0;
    if (mode != M_IDLE) {
      if (mode != prevMode) { piL.reset(); piR.reset(); }
      if (mode == M_CALIB) {
        dl = dr = 1.0f;
      } else if (fb) {
        dl = piL.update(tl / vmL, yl / vmL, dt);
        dr = piR.update(tr / vmR, yr / vmR, dt);
      } else {
        dl = tl / vmL;
        dr = tr / vmR;
        dl = dl > 1 ? 1 : (dl < -1 ? -1 : dl);
        dr = dr > 1 ? 1 : (dr < -1 ? -1 : dr);
      }
      if (doneL) { dl = 0; piL.reset(); }   // duty 0 = ブレーキ (スローディケイ)
      if (doneR) { dr = 0; piR.reset(); }
      motorsSet(dl, dr);
      if (dl > 0) dirL = 1; else if (dl < 0) dirL = -1;   // 0 のときは直前の向きを保持
      if (dr > 0) dirR = 1; else if (dr < 0) dirR = -1;
    } else {
      motorsBrake();
      piL.reset();
      piR.reset();
    }

    // --- 最高速度の計測 ---
    if (mode == M_CALIB) {
      if (mode != prevMode) { calSumL = calSumR = 0; calN = 0; }
      if (calibElapsed >= CALIB_AVG_FROM) { calSumL += yl; calSumR += yr; calN++; }
      if (calibElapsed >= CALIB_MS) {
        float avgL = calN ? calSumL / calN : 0, avgR = calN ? calSumR / calN : 0;
        Serial.printf("[drive] calib: vmaxL=%.0f vmaxR=%.0f mm/s\n", avgL, avgR);
        if (avgL > 50) shared::setParam("vmaxl", avgL);
        if (avgR > 50) shared::setParam("vmaxr", avgR);
        portENTER_CRITICAL(&mux);
        calibOn = false;
        st.calibL = avgL;
        st.calibR = avgR;
        portEXIT_CRITICAL(&mux);
      }
    }
    prevMode = mode;

    portENTER_CRITICAL(&mux);
    st.active = mode != M_IDLE;
    st.testing = mode == M_TEST;
    st.calibrating = mode == M_CALIB;
    st.feedback = fb;
    st.vmaxL = vmL;
    st.vmaxR = vmR;
    st.tgtL = tl; st.tgtR = tr;
    st.spdL = yl; st.spdR = yr;
    st.dutyL = dl; st.dutyR = dr;
    st.encL = encL.count(); st.encR = encR.count();
    if (mode != M_IDLE) {
      logBuf[logHead] = {clamp16(tl), clamp16(tr), clamp16(yl), clamp16(yr),
                         clamp16(dl * 1000), clamp16(dr * 1000)};
      logHead = (logHead + 1) % LOG_N;
      if (logCount < LOG_N) logCount++;
    }
    portEXIT_CRITICAL(&mux);
  }
}

}  // namespace

void begin() {
  // 優先度をメインループ(1)より高くし、カメラ処理に邪魔されず一定周期で回す
  xTaskCreatePinnedToCore(controlTask, "drive", 4096, nullptr, 5, nullptr, 1);
}

void setLine(bool run, float left, float right) {
  portENTER_CRITICAL(&mux);
  if (run && !lineRun && !testOn && !calibOn) clearLog();   // 走行開始でログをクリア
  lineRun = run;
  lineL = left;
  lineR = right;
  portEXIT_CRITICAL(&mux);
}

void startTest(float left, float right, uint32_t ms, FbMode fb, float revs) {
  if (ms > 10000) ms = 10000;
  shared::setRunning(false);
  portENTER_CRITICAL(&mux);
  lineRun = false;
  calibOn = false;
  testOn = true;
  testL = left;
  testR = right;
  testFb = fb;
  testUntil = millis() + ms;
  testGoal = revs > 0 ? (uint32_t)(revs * ENC_PULSES_PER_REV + 0.5f) : 0;
  testStartL = st.encL;
  testStartR = st.encR;
  if (testGoal) { st.baseL = st.encL; st.baseR = st.encR; }   // 表示の基準もここに
  clearLog();
  portEXIT_CRITICAL(&mux);
}

void startCalib() {
  shared::setRunning(false);
  portENTER_CRITICAL(&mux);
  lineRun = false;
  testOn = false;
  calibOn = true;
  calibStart = millis();
  clearLog();
  portEXIT_CRITICAL(&mux);
}

void markEncoders() {
  portENTER_CRITICAL(&mux);
  st.baseL = st.encL;
  st.baseR = st.encR;
  portEXIT_CRITICAL(&mux);
}

void stopAll() {
  shared::setRunning(false);
  portENTER_CRITICAL(&mux);
  lineRun = false;
  testOn = false;
  calibOn = false;
  portEXIT_CRITICAL(&mux);
}

State state() {
  portENTER_CRITICAL(&mux);
  State s = st;
  portEXIT_CRITICAL(&mux);
  return s;
}

size_t logJson(char* buf, size_t cap) {
  static Sample copy[LOG_N];
  int n, head;
  float vmax;
  portENTER_CRITICAL(&mux);
  n = logCount;
  head = logHead;
  vmax = st.vmaxL > st.vmaxR ? st.vmaxL : st.vmaxR;
  memcpy(copy, logBuf, sizeof(copy));
  portEXIT_CRITICAL(&mux);

  const char* names[6] = {"tl", "tr", "sl", "sr", "dl", "dr"};
  size_t k = snprintf(buf, cap, "{\"hz\":%d,\"n\":%d,\"vmax\":%.0f", CONTROL_HZ, n, vmax);
  for (int f = 0; f < 6 && k < cap; f++) {
    k += snprintf(buf + k, cap - k, ",\"%s\":[", names[f]);
    for (int i = 0; i < n && k < cap; i++) {
      const Sample& s = copy[(head - n + i + LOG_N) % LOG_N];
      const int16_t v[6] = {s.tl, s.tr, s.sl, s.sr, s.dl, s.dr};
      k += snprintf(buf + k, cap - k, i ? ",%d" : "%d", v[f]);
    }
    if (k < cap) k += snprintf(buf + k, cap - k, "]");
  }
  if (k < cap) k += snprintf(buf + k, cap - k, "}");
  return k < cap ? k : cap - 1;
}

}  // namespace drive
