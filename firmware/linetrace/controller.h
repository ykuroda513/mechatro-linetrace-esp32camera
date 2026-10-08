// =============================================================
//  controller.h  ライン位置 → 左右モータ速度 (PD制御)
//  Arduinoに依存しないのでPCでテスト可能
// =============================================================
#pragma once
#include "line_detector.h"

namespace ctrl {

struct Gains {
  float kp = 0.8f;
  float kd = 0.06f;
  float baseSpeed = 0.45f;
  float maxSpeed = 0.9f;
  float curveSlow = 0.5f;   // |偏差|=1 のとき baseSpeed をこの割合だけ下げる
  float lookahead = 0.3f;   // 遠方帯の重み
  float lostTurn = 0.6f;    // 見失ったときの旋回の強さ
  float dFilter = 0.5f;     // 微分項のローパス係数 (0:なし .. 1未満)
};

struct Output {
  float left = 0, right = 0;
  float error = 0;      // 制御に使った偏差 (-1..1, 正=線が右)
  float p = 0, d = 0;   // 各項の寄与
  bool lost = false;
};

class LineController {
public:
  void setGains(const Gains& g) { g_ = g; }
  const Gains& gains() const { return g_; }
  void reset();
  // dt: 前回からの経過時間 [s]
  Output update(const line::Result& r, float dt);

private:
  Gains g_;
  float prevErr_ = 0;
  float dFilt_ = 0;
  bool first_ = true;
};

}  // namespace ctrl
