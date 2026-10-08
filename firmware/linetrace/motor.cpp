// =============================================================
//  motor.cpp
//
//  DRV8835 IN/INモード (MODE=LOW, D0から出力) の真理値表
//    IN1  IN2   出力
//    0    0     コースト(空転)
//    PWM  0     正転 / オフ期間コースト  … ファストディケイ
//    1    PWM   正転 / オフ期間ブレーキ  … スローディケイ (IN2のデューティ = 1-速度)
//    0    PWM   逆転 / オフ期間コースト
//    PWM  1     逆転 / オフ期間ブレーキ
//    1    1     ブレーキ
//
//  arduino-esp32 v3.x の LEDC API (ledcAttachChannel / ledcWrite(pin, duty)) を使用
// =============================================================
#include "motor.h"
#include "config.h"

static constexpr uint32_t PWM_MAX = (1u << MOTOR_PWM_BITS) - 1;

Motor motorL(PIN_L_IN1, PIN_L_IN2, MOTOR_L_INVERT, 0, 1);
Motor motorR(PIN_R_IN1, PIN_R_IN2, MOTOR_R_INVERT, 2, 3);

Motor::Motor(int pinIn1, int pinIn2, bool invert, int ch1, int ch2)
    : in1_(pinIn1), in2_(pinIn2), ch1_(ch1), ch2_(ch2), invert_(invert) {}

void Motor::begin() {
  ledcAttachChannel(in1_, MOTOR_PWM_FREQ, MOTOR_PWM_BITS, ch1_);
  ledcAttachChannel(in2_, MOTOR_PWM_FREQ, MOTOR_PWM_BITS, ch2_);
  coast();
}

void Motor::write(int pin, uint32_t duty) {
  ledcWrite(pin, duty > PWM_MAX ? PWM_MAX : duty);
}

void Motor::set(float speed) {
  if (speed > 1.0f) speed = 1.0f;
  if (speed < -1.0f) speed = -1.0f;
  speed_ = speed;
  if (invert_) speed = -speed;

  const bool forward = speed >= 0.0f;
  const uint32_t d = (uint32_t)(fabsf(speed) * PWM_MAX + 0.5f);

#if MOTOR_SLOW_DECAY
  // 片側を常時High、もう片側を (1-速度) でPWM
  const uint32_t inv = PWM_MAX - d;
  if (forward) { write(in1_, PWM_MAX); write(in2_, inv); }
  else         { write(in1_, inv);     write(in2_, PWM_MAX); }
#else
  // 片側をLow、もう片側を 速度 でPWM
  if (forward) { write(in1_, d); write(in2_, 0); }
  else         { write(in1_, 0); write(in2_, d); }
#endif
}

void Motor::brake() { speed_ = 0; write(in1_, PWM_MAX); write(in2_, PWM_MAX); }
void Motor::coast() { speed_ = 0; write(in1_, 0);       write(in2_, 0); }

void motorsBegin() {
  // MODE=LOW → DRV8835 を IN/IN モードにする (PWMより先に確定させる)
  pinMode(PIN_MOTOR_MODE, OUTPUT);
  digitalWrite(PIN_MOTOR_MODE, LOW);
  motorL.begin();
  motorR.begin();
}
void motorsSet(float left, float right){ motorL.set(left); motorR.set(right); }
void motorsBrake()                     { motorL.brake(); motorR.brake(); }
void motorsCoast()                     { motorL.coast(); motorR.coast(); }
