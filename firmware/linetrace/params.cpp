// =============================================================
//  params.cpp
// =============================================================
#include "params.h"
#include <Preferences.h>
#include <freertos/semphr.h>
#include "config.h"

const ParamDesc kParamTable[] = {
  // key          label                    member                 min    max    step
  {"kp",          "Kp 比例ゲイン",          &Params::kp,           0.0f,  3.0f,  0.01f},
  {"kd",          "Kd 微分ゲイン",          &Params::kd,           0.0f,  0.5f,  0.005f},
  {"base",        "基準速度",               &Params::baseSpeed,    0.0f,  1.0f,  0.01f},
  {"max",         "速度上限",               &Params::maxSpeed,     0.0f,  1.0f,  0.01f},
  {"slow",        "カーブ減速率",           &Params::curveSlow,    0.0f,  1.0f,  0.01f},
  {"look",        "先読み(遠方の重み)",     &Params::lookahead,    0.0f,  1.0f,  0.01f},
  {"lost",        "見失い時の旋回",         &Params::lostTurn,     0.0f,  1.0f,  0.01f},
  {"exposure",    "露出",                   &Params::exposure,     0.0f,  1200.f, 10.f},
  {"gain",        "ゲイン",                 &Params::gain,         0.0f,  30.0f, 1.0f},
  {"contrast",    "最小コントラスト",       &Params::minContrast,  0.0f,  128.f, 1.0f},
  {"dark",        "黒線=1 / 白線=0",        &Params::darkLine,     0.0f,  1.0f,  1.0f},
};
const int kParamCount = sizeof(kParamTable) / sizeof(kParamTable[0]);

static const Params kDefault = {
  DEF_KP, DEF_KD, DEF_BASE_SPEED, DEF_MAX_SPEED, DEF_CURVE_SLOW, DEF_LOOKAHEAD, 0.6f,
  (float)DEF_EXPOSURE, (float)DEF_GAIN, (float)DEF_MIN_CONTRAST, (float)DEF_LINE_DARK,
};

static const uint32_t kNvsVersion = 1;   // Params 構造体を変えたら +1

namespace {
SemaphoreHandle_t mtx;
Params params = kDefault;
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
  // PSRAMがあればPSRAM、なければ内蔵RAMに確保
  frameBuf = (uint8_t*)(psramFound() ? ps_malloc(FRAME_CAP) : malloc(FRAME_CAP));

  Preferences p;
  if (p.begin("linetrace", true)) {
    if (p.getUInt("ver", 0) == kNvsVersion && p.getBytesLength("params") == sizeof(Params)) {
      p.getBytes("params", &params, sizeof(Params));
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

void resetParams() { Lock l; params = kDefault; camDirty = true; }

bool saveParams() {
  Params copy = getParams();
  Preferences p;
  if (!p.begin("linetrace", false)) return false;
  p.putUInt("ver", kNvsVersion);
  size_t n = p.putBytes("params", &copy, sizeof(Params));
  p.end();
  return n == sizeof(Params);
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
