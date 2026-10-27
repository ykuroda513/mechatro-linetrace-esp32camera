// =============================================================
//  speed_ctrl.h  車輪1つ分の速度 PI 制御 (フィードフォワード付き)
//  Arduinoに依存しないのでPCでテスト可能
//
//  速度は「最高速度に対する割合」(-1..1) で扱う。
//    目標 r, 実測 y = 実測[count/s] / maxCps
//    duty = ff*r + Kp*(r - y) + I,   I += Ki*(r - y)*dt
//  ff (フィードフォワード) が「この速度ならこのくらいのduty」という見込み、
//  PI がそのずれ (負荷・電池電圧・個体差) を補正する。
// =============================================================
#pragma once

namespace spd {

struct Gains {
  float kp = 0.5f;
  float ki = 6.0f;
  float ff = 1.0f;       // フィードフォワード係数 (0 で純粋なPI)
  float iLimit = 1.0f;   // 積分項の上限。duty の全範囲を補えるよう 1.0 (ワインドアップは条件付き積分で防ぐ)
};

class SpeedPI {
public:
  void setGains(const Gains& g) { g_ = g; }
  void reset() { i_ = 0; }
  // target, measured: -1..1 (最高速度比), dt[s] → duty (-1..1)
  float update(float target, float measured, float dt);
  float integral() const { return i_; }

private:
  Gains g_;
  float i_ = 0;
};

}  // namespace spd
