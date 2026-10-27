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
  // 速度フィードバック
  float spdFb, vmaxL, vmaxR, spdKp, spdKi, spdFf;   // vmax: 左右それぞれの duty100% 時の速度
  // カメラ・画像処理
  float autoExp, aeLevel, exposure, gain, brightness, contrast, minContrast, darkLine;
  // モータテストの設定 (Webページのモータテスト欄。ロボット側に持つので表示と実際が必ず一致する)
  float testL, testR, testMs, testRev;
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
  uint32_t encL = 0, encR = 0;    // エンコーダ累積パルス数 (常に増加)
  float spdL = 0, spdR = 0;       // 車輪速度 [mm/s]
  float vm = 0;                   // モータ電源電圧 [V]
  int   lumaMean = 0, lumaMax = 0;  // 画像の平均/最大輝度 (白飛び確認用)
  uint32_t frameNo = 0;
};

namespace shared {
void begin();               // ミューテックス生成・NVSからロード
Params getParams();
bool setParam(const char* key, float value);   // 範囲外はクリップ
void resetParams();
bool saveParams();          // NVSへ保存
bool unsaved();             // NVSに保存した内容と現在値が違うか
uint32_t bootId();          // 起動ごとに変わる番号 (Webページが再起動を検出して表示を読み直す用)

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
