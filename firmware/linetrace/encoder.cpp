// =============================================================
//  encoder.cpp
// =============================================================
#include "encoder.h"
#include "config.h"

Encoder encL(PIN_ENC_L);
Encoder encR(PIN_ENC_R);

void IRAM_ATTR Encoder::isr(void* arg) {
  Encoder* e = static_cast<Encoder*>(arg);
  e->count_ += e->dir_;
}

void Encoder::begin() {
  pinMode(pin_, INPUT);   // 74HC132 の出力 (プッシュプル) なのでプルアップ不要
  attachInterruptArg(digitalPinToInterrupt(pin_), isr, this,
                     ENC_COUNT_BOTH_EDGES ? CHANGE : RISING);
}

void Encoder::setDirection(float command) {
  if (command > 0) dir_ = 1;
  else if (command < 0) dir_ = -1;
}

void Encoder::reset() {
  noInterrupts();
  count_ = 0;
  interrupts();
  prevCount_ = 0;
  speed_ = 0;
}

float Encoder::update(float dt) {
  int32_t c = count_;
  if (dt > 1e-4f) {
    float raw = (c - prevCount_) / dt;
    speed_ = 0.7f * speed_ + 0.3f * raw;   // 軽いローパス
  }
  prevCount_ = c;
  return speed_;
}

void encodersBegin() {
  encL.begin();
  encR.begin();
}
