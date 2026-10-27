// =============================================================
//  speed_ctrl.cpp
// =============================================================
#include "speed_ctrl.h"

namespace spd {

static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

float SpeedPI::update(float target, float measured, float dt) {
  const float e = target - measured;
  const float base = g_.ff * target + g_.kp * e;
  float u = base + i_;
  // 条件付き積分: 出力が飽和していて、さらに飽和を深める方向なら積分しない
  const bool satHi = u >= 1.0f && e > 0;
  const bool satLo = u <= -1.0f && e < 0;
  if (!satHi && !satLo && dt > 0) {
    i_ = clampf(i_ + g_.ki * e * dt, -g_.iLimit, g_.iLimit);
    u = base + i_;
  }
  return clampf(u, -1.0f, 1.0f);
}

}  // namespace spd
