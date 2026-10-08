// =============================================================
//  motor.h  DRV8835 (IN/INモード) による左右DCモータ駆動
// =============================================================
#pragma once
#include <Arduino.h>

class Motor {
public:
  Motor(int pinIn1, int pinIn2, bool invert, int ch1, int ch2);
  void begin();
  // speed: -1.0 (後退最大) .. 0 .. +1.0 (前進最大)
  void set(float speed);
  void brake();   // IN1=IN2=1 : 短絡ブレーキ
  void coast();   // IN1=IN2=0 : 惰性(空転)
  float speed() const { return speed_; }

private:
  void write(int pin, uint32_t duty);
  int in1_, in2_;
  int ch1_, ch2_;   // LEDCチャネル (カメラが ch7 を使うので 0..3 を明示割当)
  bool invert_;
  float speed_ = 0.0f;
};

// 左右まとめて扱うヘルパ
void motorsBegin();
void motorsSet(float left, float right);
void motorsBrake();
void motorsCoast();
extern Motor motorL, motorR;
