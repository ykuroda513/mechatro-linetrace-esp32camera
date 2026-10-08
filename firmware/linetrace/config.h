// =============================================================
//  config.h  ハードウェア設定とパラメータ初期値
//  ここを書き換えれば配線・初期パラメータを変更できます
// =============================================================
#pragma once

// ---------- Wi-Fi (調整用Webページ) ----------
// 既存のアクセスポイントに接続します (ステーションモード)。
// SSID/パスワードは wifi_secrets.h に書きます (Gitには入りません)。
//   wifi_secrets.example.h を wifi_secrets.h にコピーして編集してください。
// 接続できなかったときは、ロボット自身がアクセスポイント "LineTrace-XXXX" になります。
#define WIFI_ENABLE          1        // 0にするとWi-Fiを使わない
#define WIFI_STA_TIMEOUT_MS  15000    // この時間内に接続できなければAPモードへ
#define WIFI_HOSTNAME_PREFIX "linetrace-"  // http://linetrace-XXXX.local/ でアクセス可能
#define WIFI_AP_SSID_PREFIX  "LineTrace-"
#define WIFI_AP_PASSWORD     "mechatro"    // APモード時のパスワード (8文字以上)

#if __has_include("wifi_secrets.h")
#include "wifi_secrets.h"
#endif
#ifndef WIFI_STA_SSID
#define WIFI_STA_SSID     ""   // 空ならすぐAPモードで起動
#define WIFI_STA_PASSWORD ""
#endif

// ---------- モータドライバ DRV8835 (IN/INモード) ----------
//  回路図「Roomba CPU基板」「モータドライバ・エンコーダ回路図」(2026-10-08) に準拠
//
//  J3   XIAO  GPIO   DRV8835 (AE-DRV8835-S)
//  1    3V3   -      VCC
//  2    D0    1      MODE   … 起動時に LOW を出力して IN/IN モードにする
//  3    D1    2      AIN1   ┐ A ch → J5 → M1 (左)
//  4    D2    3      AIN2   ┘
//  5    D3    4      BIN1   ┐ B ch → J6 → M2 (右)
//  6    D4    5      BIN2   ┘
//  7    GND   -      GND
//  8    D9    8      VM_SENSE (VM を 4.7k/4.7k で 1/2 に分圧)
#define PIN_MOTOR_MODE 1   // D0
#define PIN_A_IN1      2   // D1
#define PIN_A_IN2      3   // D2
#define PIN_B_IN1      4   // D3
#define PIN_B_IN2      5   // D4

// ---------- エンコーダ (フォトインタラプタ LBT-131 + 74HC132 で整形, 1相) ----------
//  J4A.2  → D6 / GPIO43 … 左車輪
//  J4B.2  → D5 / GPIO6  … 右車輪
//  (PS1 = J4A = 左。回路図のPS番号とJ4の対応表記とは逆なので注意)
//  1相なので回転方向は検出できない。方向はモータへの指令の符号から決める。
#define PIN_ENC_L 43   // D6 (注意: UART0 TX と共用。起動時に一瞬ログが出力される)
#define PIN_ENC_R 6    // D5
#define ENC_COUNT_BOTH_EDGES 1   // 1: 立上り/立下り両方を数える (分解能2倍)
#define ENC_PULSES_PER_REV  0    // 車輪1回転あたりのカウント数 (0=未設定, rpm表示しない)

// ---------- 左右モータ ----------
//  左 = M1 (A ch, J5)、右 = M2 (B ch, J6)
#define PIN_L_IN1 PIN_A_IN1
#define PIN_L_IN2 PIN_A_IN2
#define PIN_R_IN1 PIN_B_IN1
#define PIN_R_IN2 PIN_B_IN2

// ---------- モータ電源電圧 ----------
#define PIN_VM_SENSE   8      // D9 (ADC1_CH7)
#define VM_SENSE_RATIO 2.0f   // VM = 測定電圧 × 2 (R5=R6=4.7kΩ)
// ADCの上限は約3.1Vなので、測れるのは VM ≦ 約6.2V まで

// モータの回転方向が逆なら 1 にする（配線を入れ替えずに済む）
#define MOTOR_L_INVERT 0
#define MOTOR_R_INVERT 0

#define MOTOR_PWM_FREQ 20000  // [Hz] 可聴域外
#define MOTOR_PWM_BITS 10     // 分解能 (0..1023)

// 減衰モード
//  1: スローディケイ(ブレーキ) … PWMオフ期間に短絡ブレーキ。速度がデューティにほぼ比例し制御しやすい
//  0: ファストディケイ(コースト) … PWMオフ期間は惰性
#define MOTOR_SLOW_DECAY 1

// ---------- スイッチ / LED ----------
#define PIN_START_BUTTON 0    // 基板上のBOOTボタン (押すとLOW)
#define PIN_USER_LED     21   // 基板上のオレンジLED (LOWで点灯)
// 注意: GPIO21 は Sense の microSD CS と共用です

// ---------- 安全 ----------
#define LOST_TIMEOUT_MS 1500  // この時間ラインを見失い続けたら停止

// ---------- カメラ ----------
// XCLK を上げすぎると "cam_hal: FB-OVF" (DMAあふれ) でフレームが欠ける。
// OV2640 + グレースケールでは 10MHz が安定 (それでも出るなら 8MHz などに下げる)
#define CAM_XCLK_HZ   10000000
// 0: GRAYSCALE で取得 (1byte/画素)
// 1: YUV422 で取得して輝度(Y)だけ取り出す。GRAYSCALE で
//    "FB-SIZE: xxx != 19200" が出続けるときに試す
#define CAM_USE_YUV422 0
#define CAM_VFLIP     0       // 取り付け向きに合わせて 0/1
#define CAM_HMIRROR   0

// ---------- パラメータ初期値 (Webページから変更・保存可能) ----------
#define DEF_KP          0.80f   // 比例ゲイン   [速度/偏差]
#define DEF_KD          0.06f   // 微分ゲイン   [速度*s/偏差]
#define DEF_BASE_SPEED  0.45f   // 直進速度     (0..1)
#define DEF_MAX_SPEED   0.90f   // 速度上限     (0..1)
#define DEF_CURVE_SLOW  0.50f   // 偏差が大きいときの減速率 (0..1)
#define DEF_LOOKAHEAD   0.30f   // 遠方ROIの重み (0=手前だけ, 1=遠方だけ)
#define DEF_EXPOSURE    300     // 露出 (0..1200)
#define DEF_GAIN        2       // ゲイン (0..30)
#define DEF_MIN_CONTRAST 30     // これ未満のコントラストは「線なし」と判定
#define DEF_LINE_DARK   1       // 1: 白地に黒線, 0: 黒地に白線
