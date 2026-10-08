// =============================================================
//  params.h  調整パラメータとロボットの共有状態
//
//  パラメータは表 (kParamTable) で定義しているので、
//  1行追加するだけでWebページにスライダーが増えます
// =============================================================
#pragma once
#include <Arduino.h>
#include "line_detector.h"
#include "controller.h"

struct Params {
  // 制御
  float kp, kd, baseSpeed, maxSpeed, curveSlow, lookahead, lostTurn;
  // カメラ・画像処理
  float exposure, gain, minContrast, darkLine;
};

struct ParamDesc {
  const char* key;    // URL/JSONで使う名前
  const char* label;  // 画面表示
  float Params::*member;
  float min, max, step;
};

extern const ParamDesc kParamTable[];
extern const int kParamCount;

// --- 共有状態 (制御ループ ⇔ Webサーバ) ---
struct Telemetry {
  line::Result det;
  ctrl::Output out;
  float fps = 0;
  int32_t encL = 0, encR = 0;     // エンコーダ累積カウント
  float spdL = 0, spdR = 0;       // エンコーダ速度 [count/s]
  float vm = 0;                   // モータ電源電圧 [V]
  uint32_t frameNo = 0;
};

namespace shared {
void begin();               // ミューテックス生成・NVSからロード
Params getParams();
bool setParam(const char* key, float value);   // 範囲外はクリップ
void resetParams();
bool saveParams();          // NVSへ保存

bool running();
bool cameraOk();
void setCameraOk(bool ok);
void setRunning(bool on);

void putTelemetry(const Telemetry& t);
Telemetry getTelemetry();

// 最新フレーム (グレースケール) のコピー
void putFrame(const uint8_t* img, int w, int h);
// buf に最大 cap バイトコピー。戻り値: コピーしたバイト数 (0=未取得)
size_t getFrame(uint8_t* buf, size_t cap, int* w, int* h);

// パラメータ変更があったか (カメラ設定の再適用用)。呼ぶとフラグはクリア
bool consumeCameraDirty();
}  // namespace shared
