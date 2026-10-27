// =============================================================
//  camera.h  XIAO ESP32S3 Sense 付属カメラ (OV2640 / OV3660)
// =============================================================
#pragma once
#include <esp_camera.h>

bool cameraBegin();
// 露出・ゲイン・画質補正を設定する
//  autoExp=true : 自動露出・自動ゲイン (aeLevel で明るさの目標を調整)
//  autoExp=false: 露出・ゲインを固定 (床の明るさでしきい値が揺れにくい)
void cameraSetExposure(bool autoExp, int aeLevel, int exposure, int gain,
                       int brightness, int contrast);
// フレームからグレースケール画像を取り出す。サイズ異常のフレームは nullptr
// (YUV422 モードでは内部バッファに Y を抜き出して返す)
const uint8_t* cameraGray(const camera_fb_t* fb, int* w, int* h);
