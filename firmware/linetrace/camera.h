// =============================================================
//  camera.h  XIAO ESP32S3 Sense 付属カメラ (OV2640 / OV3660)
// =============================================================
#pragma once
#include <esp_camera.h>

bool cameraBegin();
// 露出・ゲインを固定値にする (自動露出だと床の明るさでしきい値が揺れるため)
void cameraSetExposure(int exposure, int gain);
// フレームからグレースケール画像を取り出す。サイズ異常のフレームは nullptr
// (YUV422 モードでは内部バッファに Y を抜き出して返す)
const uint8_t* cameraGray(const camera_fb_t* fb, int* w, int* h);
