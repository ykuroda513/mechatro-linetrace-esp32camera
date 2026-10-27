// =============================================================
//  encoder.cpp
// =============================================================
#include "encoder.h"
#include "config.h"
#if ENC_USE_PCNT
#include <driver/pulse_cnt.h>
#endif

Encoder encL(PIN_ENC_L);
Encoder encR(PIN_ENC_R);

static constexpr int PCNT_HIGH = 30000;   // この値に達すると 0 に戻る

void IRAM_ATTR Encoder::isr(void* arg) {
  Encoder* e = static_cast<Encoder*>(arg);
  e->isrCount_ = e->isrCount_ + 1;
}

bool Encoder::begin() {
  pinMode(pin_, INPUT);   // 74HC132 の出力 (プッシュプル) なのでプルアップ不要
#if ENC_USE_PCNT
  pcnt_unit_config_t uc = {};
  uc.low_limit = -1;          // 減ることはないが、仕様上 負の値が必要
  uc.high_limit = PCNT_HIGH;
  pcnt_unit_handle_t unit = nullptr;
  if (pcnt_new_unit(&uc, &unit) != ESP_OK) {
    Serial.printf("[enc] pcnt_new_unit failed (pin %d)\n", pin_);
    return false;
  }
  pcnt_glitch_filter_config_t fc = {};
  fc.max_glitch_ns = ENC_GLITCH_NS;
  pcnt_unit_set_glitch_filter(unit, &fc);

  pcnt_chan_config_t cc = {};
  cc.edge_gpio_num = pin_;
  cc.level_gpio_num = -1;
  pcnt_channel_handle_t ch = nullptr;
  if (pcnt_new_channel(unit, &cc, &ch) != ESP_OK) {
    Serial.printf("[enc] pcnt_new_channel failed (pin %d)\n", pin_);
    return false;
  }
  pcnt_channel_set_edge_action(ch, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                               ENC_COUNT_BOTH_EDGES ? PCNT_CHANNEL_EDGE_ACTION_INCREASE
                                                    : PCNT_CHANNEL_EDGE_ACTION_HOLD);
  pcnt_unit_enable(unit);
  pcnt_unit_clear_count(unit);
  pcnt_unit_start(unit);
  unit_ = unit;
  Serial.printf("[enc] PCNT on GPIO%d (glitch filter %d ns)\n", pin_, ENC_GLITCH_NS);
#else
  attachInterruptArg(digitalPinToInterrupt(pin_), isr, this,
                     ENC_COUNT_BOTH_EDGES ? CHANGE : RISING);
#endif
  prevRaw_ = (int)readRaw();
  return true;
}

uint32_t Encoder::readRaw() {
#if ENC_USE_PCNT
  int v = 0;
  if (unit_) pcnt_unit_get_count((pcnt_unit_handle_t)unit_, &v);
  return (uint32_t)v;
#else
  return isrCount_;
#endif
}

float Encoder::update(float dt) {
  const int raw = (int)readRaw();
  int d = raw - prevRaw_;
#if ENC_USE_PCNT
  if (d < 0) d += PCNT_HIGH;   // 上限に達して 0 に戻った
#endif
  prevRaw_ = raw;
  total_ = total_ + (uint32_t)d;
  if (dt > 1e-4f) speed_ = 0.5f * speed_ + 0.5f * (d / dt);   // 軽いローパス (100Hz前提)
  return speed_;
}

void encodersBegin() {
  encL.begin();
  encR.begin();
}
