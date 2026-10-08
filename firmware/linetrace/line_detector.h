// =============================================================
//  line_detector.h  グレースケール画像からラインの位置を求める
//
//  Arduinoに依存しない純粋なC++なので、PC上でもテストできます (test/ 参照)
//
//  手順
//   1. 画像全体の輝度ヒストグラムから大津の方法でしきい値を決める
//      (2クラスの平均輝度の差 = コントラスト が小さければ「線なし」)
//   2. ROI(横長の帯)ごとに、各列の「線の画素」の数を数える
//   3. 線の画素が帯の高さの colFill 以上ある列を「線の列」とし、
//      連続する区間(セグメント)を探す
//   4. 前回の位置に最も近いセグメントを選び、画素数で重み付けした重心を位置とする
//
//  画像全体でしきい値を決めるので、交差点のように帯全体が線で覆われても
//  正しく「線」と判定できます (coverage が 1 に近くなる)
// =============================================================
#pragma once
#include <stdint.h>

namespace line {

constexpr int MAX_WIDTH = 320;
constexpr int NUM_BANDS = 3;   // 0:手前(画像下) 1:中間 2:遠方(画像上)

struct BandResult {
  bool  found = false;
  float pos = 0.0f;       // -1(左端) .. 0(中央) .. +1(右端)
  float width = 0.0f;     // 選んだセグメントの幅 / 画像幅 (0..1)
  float coverage = 0.0f;  // 線の列の割合 (交差点・ゴール判定用)
  int   contrast = 0;   // 線と背景の平均輝度の差
  int   threshold = 0;  // 二値化しきい値 (輝度)
  int   y0 = 0, y1 = 0;   // 帯の行範囲 [y0, y1)
};

struct Result {
  BandResult band[NUM_BANDS];
};

struct Config {
  bool darkLine = true;     // 白地に黒線なら true
  int  minContrast = 30;
  float colFill = 0.3f;     // 列を「線」とみなす線画素の割合
  // 各帯の行範囲 (画像高さに対する割合, 0=上端 1=下端)
  float bandTop[NUM_BANDS]    = {0.75f, 0.50f, 0.25f};
  float bandBottom[NUM_BANDS] = {0.92f, 0.62f, 0.37f};
};

class Detector {
public:
  void setConfig(const Config& c) { cfg_ = c; }
  const Config& config() const { return cfg_; }
  // img: 8bitグレースケール, 行優先, width<=MAX_WIDTH
  Result process(const uint8_t* img, int width, int height);
  void reset();

private:
  int otsu(const uint8_t* img, int n, int* contrast);
  BandResult processBand(const uint8_t* img, int w, int y0, int y1, int thr, float prevPos);
  Config cfg_;
  float prevPos_[NUM_BANDS] = {0, 0, 0};
  uint16_t profile_[MAX_WIDTH];
};

}  // namespace line
