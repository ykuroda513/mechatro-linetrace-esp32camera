// =============================================================
//  controller.cpp
//
//  偏差 e (正=線が右)
//  u = Kp*e + Kd*de/dt
//  v = base * (1 - curveSlow*|e|)
//  左 = v + u, 右 = v - u   (線が右にあれば左を速く → 右に曲がる)
// =============================================================
#include "controller.h"
#include <math.h>

namespace ctrl {

static float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

void LineController::reset() {
  prevErr_ = 0;
  dFilt_ = 0;
  first_ = true;
}

Output LineController::update(const line::Result& r, float dt) {
  Output o;
  const line::BandResult& nearB = r.band[0];
  const line::BandResult& midB  = r.band[1];
  const line::BandResult& farB  = r.band[2];

  // --- 偏差の計算 (手前と遠方をブレンド) ---
  float e;
  if (nearB.found) {
    const line::BandResult* ahead = farB.found ? &farB : (midB.found ? &midB : nullptr);
    e = ahead ? (1.0f - g_.lookahead) * nearB.pos + g_.lookahead * ahead->pos : nearB.pos;
  } else if (midB.found) {
    e = midB.pos;
  } else if (farB.found) {
    e = farB.pos;
  } else {
    // --- 見失った: 最後に見えた側へその場旋回 ---
    o.lost = true;
    float dir = prevErr_ >= 0 ? 1.0f : -1.0f;
    o.error = prevErr_;
    o.left = dir * g_.lostTurn;
    o.right = -dir * g_.lostTurn;
    first_ = true;  // 再発見時に微分が跳ねないように
    return o;
  }
  e = clampf(e, -1.0f, 1.0f);

  // --- PD ---
  float deriv = 0;
  if (!first_ && dt > 1e-4f) deriv = (e - prevErr_) / dt;
  dFilt_ = g_.dFilter * dFilt_ + (1.0f - g_.dFilter) * deriv;
  first_ = false;
  prevErr_ = e;

  o.error = e;
  o.p = g_.kp * e;
  o.d = g_.kd * dFilt_;
  float u = o.p + o.d;

  float v = g_.baseSpeed * (1.0f - g_.curveSlow * fabsf(e));
  float l = v + u, rr = v - u;

  // 上限を超えたら差(旋回量)を保ったまま全体を下げる
  float m = fmaxf(fabsf(l), fabsf(rr));
  if (m > g_.maxSpeed) {
    float s = g_.maxSpeed / m;
    l *= s;
    rr *= s;
  }
  o.left = l;
  o.right = rr;
  return o;
}

}  // namespace ctrl
