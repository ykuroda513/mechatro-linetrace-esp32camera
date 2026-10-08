// =============================================================
//  encoder.h  1相エンコーダ (フォトインタラプタ) のパルスカウント
//
//  ENC信号は 74HC132 (シュミットトリガ) で整形済みのため、
//  内部プルアップなしの入力として割り込みで数える。
//  1相なので回転方向は分からない → モータへの指令の符号を使う。
// =============================================================
#pragma once
#include <Arduino.h>

class Encoder {
public:
  explicit Encoder(int pin) : pin_(pin) {}
  void begin();
  // モータ指令の符号を渡す (0 のときは直前の向きを保持)
  void setDirection(float command);
  int32_t count() const { return count_; }   // 符号付き累積カウント
  void reset();
  // 周期的に呼ぶ。dt[s] から速度 [count/s] を更新して返す
  float update(float dt);
  float speed() const { return speed_; }

private:
  static void IRAM_ATTR isr(void* arg);
  int pin_;
  volatile int32_t count_ = 0;
  volatile int8_t dir_ = 1;
  int32_t prevCount_ = 0;
  float speed_ = 0;
};

extern Encoder encL, encR;
void encodersBegin();
