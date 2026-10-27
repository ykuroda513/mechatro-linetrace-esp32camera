// =============================================================
//  params.cpp
// =============================================================
#include "params.h"
#include <Preferences.h>
#include <esp_random.h>
#include <freertos/semphr.h>
#include "config.h"

const ParamDesc kParamTable[] = {
  // key          label                    member                 min    max    step
  {"kp",          "Kp 比例ゲイン [mm/s]",   &Params::kp,           0.0f,  2000.f, 5.f},
  {"kd",          "Kd 微分ゲイン [mm/s·s]", &Params::kd,           0.0f,  300.f, 1.f},
  {"base",        "基準速度 [mm/s]",        &Params::baseSpeed,    0.0f,  1500.f, 10.f},
  {"max",         "速度上限 [mm/s]",        &Params::maxSpeed,     0.0f,  1500.f, 10.f},
  {"slow",        "カーブ減速率",           &Params::curveSlow,    0.0f,  1.0f,  0.01f},
  {"look",        "先読み(遠方の重み)",     &Params::lookahead,    0.0f,  1.0f,  0.01f},
  {"lost",        "見失い時の旋回 [mm/s]",  &Params::lostTurn,     0.0f,  1000.f, 10.f},
  {"spdfb",       "速度FB 1=ON / 0=OFF",    &Params::spdFb,        0.0f,  1.0f,  1.0f},
  {"vmaxl",       "最高速度 左 [mm/s] (duty100%)", &Params::vmaxL,  50.0f, 3000.f, 10.f},
  {"vmaxr",       "最高速度 右 [mm/s] (duty100%)", &Params::vmaxR,  50.0f, 3000.f, 10.f},
  {"spdkp",       "速度FB Kp",              &Params::spdKp,        0.0f,  3.0f,  0.01f},
  {"spdki",       "速度FB Ki",              &Params::spdKi,        0.0f,  30.0f, 0.1f},
  {"spdff",       "速度FB フィードフォワード", &Params::spdFf,     0.0f,  1.5f,  0.01f},
  {"autoexp",     "自動露出 1=ON / 0=手動", &Params::autoExp,      0.0f,  1.0f,  1.0f},
  {"aelevel",     "自動露出の明るさ",       &Params::aeLevel,     -2.0f,  2.0f,  1.0f},
  {"exposure",    "手動露出",               &Params::exposure,     0.0f,  1200.f, 1.f},
  {"gain",        "手動ゲイン",             &Params::gain,         0.0f,  30.0f, 1.0f},
  {"bright",      "明るさ補正",             &Params::brightness,  -2.0f,  2.0f,  1.0f},
  {"cont",        "コントラスト補正",       &Params::contrast,    -2.0f,  2.0f,  1.0f},
  {"contrast",    "最小コントラスト",       &Params::minContrast,  0.0f,  128.f, 1.0f},
  {"dark",        "黒線=1 / 白線=0",        &Params::darkLine,     0.0f,  1.0f,  1.0f},
  // key が "t_" で始まるものはモータテスト欄に表示 (パラメータ一覧には出さない)
  {"t_l",         "テスト 左目標 [mm/s]",   &Params::testL,    -1000.f, 1000.f, 10.f},
  {"t_r",         "テスト 右目標 [mm/s]",   &Params::testR,    -1000.f, 1000.f, 10.f},
  {"t_ms",        "テスト 時間 [ms]",       &Params::testMs,     500.f, 3000.f, 100.f},
  {"t_rev",       "テスト 回転数",          &Params::testRev,      0.0f,  20.0f, 1.0f},
};
const int kParamCount = sizeof(kParamTable) / sizeof(kParamTable[0]);
// Params のメンバはすべて表に載っていること (表示されない隠れた設定値を作らないため)
static_assert(sizeof(Params) / sizeof(float) == sizeof(kParamTable) / sizeof(kParamTable[0]),
              "Params のメンバと kParamTable の行数が一致しません");

static const Params kDefault = {
  DEF_KP, DEF_KD, DEF_BASE_SPEED, DEF_MAX_SPEED, DEF_CURVE_SLOW, DEF_LOOKAHEAD, DEF_LOST_TURN,
  (float)DEF_SPD_FB, DEF_VMAX, DEF_VMAX, DEF_SPD_KP, DEF_SPD_KI, DEF_SPD_FF,
  (float)DEF_AUTO_EXP, (float)DEF_AE_LEVEL, (float)DEF_EXPOSURE, (float)DEF_GAIN,
  (float)DEF_BRIGHTNESS, (float)DEF_CONTRAST, (float)DEF_MIN_CONTRAST, (float)DEF_LINE_DARK,
  200.0f, 200.0f, 2000.0f, 0.0f,
};

static const uint32_t kNvsVersion = 6;   // Params 構造体を変えたら +1

namespace {
SemaphoreHandle_t mtx;
Params params = kDefault;
Params savedParams = kDefault;   // NVS に入っている内容 (未保存の変更の検出用)
uint32_t bootIdValue = 0;
Telemetry telem;
bool runFlag = false;
bool camOk = false;
bool camDirty = true;

constexpr size_t FRAME_CAP = 160 * 120;   // QQVGA
uint8_t* frameBuf = nullptr;
size_t frameLen = 0;
int frameW = 0, frameH = 0;

struct Lock {
  Lock() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Lock() { xSemaphoreGive(mtx); }
};
}  // namespace

namespace shared {

void begin() {
  mtx = xSemaphoreCreateMutex();
  bootIdValue = esp_random();
  // PSRAMがあればPSRAM、なければ内蔵RAMに確保
  frameBuf = (uint8_t*)(psramFound() ? ps_malloc(FRAME_CAP) : malloc(FRAME_CAP));

  Preferences p;
  if (p.begin("linetrace", true)) {
    if (p.getUInt("ver", 0) == kNvsVersion && p.getBytesLength("params") == sizeof(Params)) {
      p.getBytes("params", &params, sizeof(Params));
      savedParams = params;
      Serial.println("[params] loaded from NVS");
    }
    p.end();
  }
}

Params getParams() { Lock l; return params; }

bool setParam(const char* key, float v) {
  for (int i = 0; i < kParamCount; i++) {
    const ParamDesc& d = kParamTable[i];
    if (strcmp(d.key, key) != 0) continue;
    if (v < d.min) v = d.min;
    if (v > d.max) v = d.max;
    Lock l;
    params.*(d.member) = v;
    camDirty = true;
    return true;
  }
  return false;
}

bool unsaved() { Lock l; return memcmp(&params, &savedParams, sizeof(Params)) != 0; }
uint32_t bootId() { return bootIdValue; }

void resetParams() { Lock l; params = kDefault; camDirty = true; }

bool saveParams() {
  Params copy = getParams();
  Preferences p;
  if (!p.begin("linetrace", false)) return false;
  p.putUInt("ver", kNvsVersion);
  size_t n = p.putBytes("params", &copy, sizeof(Params));
  p.end();
  if (n != sizeof(Params)) return false;
  Lock l;
  savedParams = copy;
  return true;
}

bool running() { Lock l; return runFlag; }
bool cameraOk() { Lock l; return camOk; }
void setCameraOk(bool ok) { Lock l; camOk = ok; }
void setRunning(bool on) { Lock l; runFlag = on; }

void putTelemetry(const Telemetry& t) { Lock l; telem = t; }
Telemetry getTelemetry() { Lock l; return telem; }

void putFrame(const uint8_t* img, int w, int h) {
  size_t n = (size_t)w * h;
  if (!frameBuf || n > FRAME_CAP) return;
  Lock l;
  memcpy(frameBuf, img, n);
  frameLen = n;
  frameW = w;
  frameH = h;
}

size_t getFrame(uint8_t* buf, size_t cap, int* w, int* h) {
  Lock l;
  if (frameLen == 0 || frameLen > cap) return 0;
  memcpy(buf, frameBuf, frameLen);
  *w = frameW;
  *h = frameH;
  return frameLen;
}

bool consumeCameraDirty() {
  Lock l;
  bool d = camDirty;
  camDirty = false;
  return d;
}

}  // namespace shared
