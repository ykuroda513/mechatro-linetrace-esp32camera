// =============================================================
//  camera.cpp
//  ピン定義は arduino-esp32 の CameraWebServer 例
//  (CAMERA_MODEL_XIAO_ESP32S3) と同じです
// =============================================================
#include "camera.h"
#include <Arduino.h>
#include "config.h"

#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  10
#define SIOD_GPIO_NUM  40
#define SIOC_GPIO_NUM  39
#define Y9_GPIO_NUM    48
#define Y8_GPIO_NUM    11
#define Y7_GPIO_NUM    12
#define Y6_GPIO_NUM    14
#define Y5_GPIO_NUM    16
#define Y4_GPIO_NUM    18
#define Y3_GPIO_NUM    17
#define Y2_GPIO_NUM    15
#define VSYNC_GPIO_NUM 38
#define HREF_GPIO_NUM  47
#define PCLK_GPIO_NUM  13

bool cameraBegin() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_7;   // モータ用PWMと被らないチャネル
  c.ledc_timer = LEDC_TIMER_3;
  c.pin_d0 = Y2_GPIO_NUM;
  c.pin_d1 = Y3_GPIO_NUM;
  c.pin_d2 = Y4_GPIO_NUM;
  c.pin_d3 = Y5_GPIO_NUM;
  c.pin_d4 = Y6_GPIO_NUM;
  c.pin_d5 = Y7_GPIO_NUM;
  c.pin_d6 = Y8_GPIO_NUM;
  c.pin_d7 = Y9_GPIO_NUM;
  c.pin_xclk = XCLK_GPIO_NUM;
  c.pin_pclk = PCLK_GPIO_NUM;
  c.pin_vsync = VSYNC_GPIO_NUM;
  c.pin_href = HREF_GPIO_NUM;
  c.pin_sccb_sda = SIOD_GPIO_NUM;
  c.pin_sccb_scl = SIOC_GPIO_NUM;
  c.pin_pwdn = PWDN_GPIO_NUM;
  c.pin_reset = RESET_GPIO_NUM;
  c.xclk_freq_hz = CAM_XCLK_HZ;
#if CAM_USE_YUV422
  c.pixel_format = PIXFORMAT_YUV422;      // Y U Y V ... (2byte/画素)
#else
  c.pixel_format = PIXFORMAT_GRAYSCALE;   // 輝度のみ (1byte/画素)
#endif
  c.frame_size = FRAMESIZE_QQVGA;         // 160x120
  c.jpeg_quality = 12;
  c.fb_count = 2;
  // 160x120 グレースケールは 19KB なので、PSRAMがなくても内蔵RAMで動く
  c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;       // 常に最新フレームを取得 (遅延を減らす)

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("[camera] init failed: 0x%x\n", err);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  Serial.printf("[camera] sensor PID=0x%x  framebuffer in %s\n", s->id.PID,
                psramFound() ? "PSRAM" : "internal RAM");
  s->set_vflip(s, CAM_VFLIP);
  s->set_hmirror(s, CAM_HMIRROR);
  return true;
}

void cameraSetExposure(bool autoExp, int aeLevel, int exposure, int gain,
                       int brightness, int contrast) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  s->set_brightness(s, brightness);
  s->set_contrast(s, contrast);
  if (autoExp) {
    s->set_exposure_ctrl(s, 1);   // 自動露出 ON
    s->set_aec2(s, 1);
    s->set_ae_level(s, aeLevel);
    s->set_gain_ctrl(s, 1);       // 自動ゲイン ON
    s->set_gainceiling(s, GAINCEILING_4X);
  } else {
    s->set_exposure_ctrl(s, 0);   // 自動露出 OFF
    s->set_aec2(s, 0);
    s->set_aec_value(s, exposure);
    s->set_gain_ctrl(s, 0);       // 自動ゲイン OFF
    s->set_agc_gain(s, gain);
  }
  Serial.printf("[camera] %s exp=%d gain=%d ae=%d bright=%d cont=%d\n",
                autoExp ? "AUTO" : "MANUAL", exposure, gain, aeLevel, brightness, contrast);
}

const uint8_t* cameraGray(const camera_fb_t* fb, int* w, int* h) {
  const size_t n = fb->width * fb->height;
#if CAM_USE_YUV422
  static uint8_t gray[160 * 120];
  if (fb->len != n * 2 || n > sizeof(gray)) return nullptr;
  for (size_t i = 0; i < n; i++) gray[i] = fb->buf[i * 2];   // 偶数バイトが Y
  *w = fb->width;
  *h = fb->height;
  return gray;
#else
  if (fb->len != n) return nullptr;   // 欠けたフレームは使わない
  *w = fb->width;
  *h = fb->height;
  return fb->buf;
#endif
}
