// =============================================================
//  drive.h  モータ駆動タスク (100Hz)
//
//  カメラのフレーム周期とは独立に、一定周期でエンコーダ速度を計算し、
//  速度フィードバック (PI制御) をかけてモータに出力する。
//  速度はすべて車輪の周速 = 前進速度 [mm/s] で扱う。
//
//  指令の出どころ:
//    - ライン追従 (メインループ)  setLine()
//    - モータテスト (Webページ)   startTest()   … 指定時間で自動停止
//    - 最高速度の計測            startCalib()  … duty 100% で回して vmax を自動設定
// =============================================================
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace drive {

constexpr int CONTROL_HZ = 100;

enum FbMode { FB_PARAM = -1, FB_OFF = 0, FB_ON = 1 };

struct State {
  bool active = false;      // モータに出力中
  bool testing = false;     // モータテスト中
  bool calibrating = false; // 最高速度の計測中
  bool feedback = false;    // 速度FB有効
  float vmaxL = 0, vmaxR = 0;  // duty 100% 時の速度 [mm/s] (パラメータ, 左右別)
  float tgtL = 0, tgtR = 0; // 目標速度 [mm/s]
  float spdL = 0, spdR = 0; // 実測速度 [mm/s] (指令の向きで符号付け)
  float dutyL = 0, dutyR = 0;  // -1..1
  uint32_t encL = 0, encR = 0; // 累積パルス数
  uint32_t baseL = 0, baseR = 0; // markEncoders() したときのパルス数
  float calibL = 0, calibR = 0;  // 直近の最高速度計測結果 [mm/s]
};

void begin();
// ライン追従からの指令 [mm/s]。run=false なら停止 (テスト中は無視)
void setLine(bool run, float leftMmS, float rightMmS);
// モータテスト開始 [mm/s]。ms 後に自動停止。fb: 速度FBの使い方
// revs > 0 なら、各車輪がエンコーダで revs 回転分数えた時点でその車輪を止める
// (エンコーダが正しく数えているかを、実際の車輪の回転数と見比べて確かめる用)
void startTest(float leftMmS, float rightMmS, uint32_t ms, FbMode fb = FB_PARAM,
               float revs = 0);
// duty 100% で 1.5 秒回し、その間の速度から左右それぞれの vmax を設定する
void startCalib();
void stopAll();
// 現在のパルス数を基準にする (手で車輪を1回転させてカウント数を確かめる用)
void markEncoders();
State state();
// 直近のログ (100Hz, 3秒) を JSON で buf に書く。戻り値: 書いた長さ
size_t logJson(char* buf, size_t cap);

}  // namespace drive
