// =============================================================
//  encoder.h  1相エンコーダ (フォトインタラプタ) のパルスカウント
//
//  ENC信号は 74HC132 (シュミットトリガ) で整形済み。
//  ENC_USE_PCNT=1 (既定) では ESP32-S3 のハードウェアパルスカウンタ (PCNT) で数え、
//  グリッチフィルタで短いノイズパルスを除く。0 なら GPIO 割り込みで数える。
//
//  1相なので回転方向は分からない。カウントは常に増える一方 (符号なし) で、
//  速度も大きさ (>=0) だけを返す。向きは drive.cpp がモータ指令の符号から付ける。
//  update() は drive タスクだけが呼ぶこと。
// =============================================================
#pragma once
#include <Arduino.h>

class Encoder {
public:
  explicit Encoder(int pin) : pin_(pin) {}
  bool begin();
  uint32_t count() const { return total_; }  // 累積パルス数 (常に増加)
  // 周期的に呼ぶ。dt[s] から速さ [count/s] (>=0) を更新して返す
  float update(float dt);
  float speed() const { return speed_; }

private:
  uint32_t readRaw();   // ハードウェア/割り込みカウンタの生の値
  static void IRAM_ATTR isr(void* arg);
  int pin_;
  void* unit_ = nullptr;          // pcnt_unit_handle_t
  volatile uint32_t isrCount_ = 0;
  int prevRaw_ = 0;
  volatile uint32_t total_ = 0;
  float speed_ = 0;
};

extern Encoder encL, encR;
void encodersBegin();
