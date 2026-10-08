// =============================================================
//  line_detector.cpp
// =============================================================
#include "line_detector.h"

namespace line {

void Detector::reset() {
  for (int i = 0; i < NUM_BANDS; i++) prevPos_[i] = 0.0f;
}

// 大津の二値化: クラス間分散が最大になるしきい値を求める
// 戻り値: しきい値 t (輝度 <= t が暗いクラス)。contrast に2クラスの平均差を返す
int Detector::otsu(const uint8_t* img, int n, int* contrast) {
  uint32_t hist[256] = {0};
  for (int i = 0; i < n; i += 2) hist[img[i]]++;   // 1画素おきで十分
  uint32_t total = 0;
  uint64_t sumAll = 0;
  for (int v = 0; v < 256; v++) { total += hist[v]; sumAll += (uint64_t)v * hist[v]; }

  uint32_t w0 = 0;
  uint64_t sum0 = 0;
  double best = -1;
  int bestT = 127;
  float m0Best = 0, m1Best = 0;
  for (int t = 0; t < 255; t++) {
    w0 += hist[t];
    sum0 += (uint64_t)t * hist[t];
    if (w0 == 0) continue;
    uint32_t w1 = total - w0;
    if (w1 == 0) break;
    double m0 = (double)sum0 / w0;
    double m1 = (double)(sumAll - sum0) / w1;
    double between = (double)w0 * w1 * (m1 - m0) * (m1 - m0);
    if (between > best) {
      best = between;
      bestT = t;
      m0Best = (float)m0;
      m1Best = (float)m1;
    }
  }
  *contrast = (int)(m1Best - m0Best + 0.5f);
  return bestT;
}

Result Detector::process(const uint8_t* img, int w, int h) {
  Result r;
  if (w > MAX_WIDTH || w <= 0 || h <= 0) return r;   // 想定外のサイズ
  int contrast = 0;
  const int thr = otsu(img, w * h, &contrast);
  const bool enough = contrast >= cfg_.minContrast;

  for (int b = 0; b < NUM_BANDS; b++) {
    int y0 = (int)(cfg_.bandTop[b] * h);
    int y1 = (int)(cfg_.bandBottom[b] * h);
    if (y0 < 0) y0 = 0;
    if (y1 > h) y1 = h;
    if (y1 <= y0) y1 = y0 + 1;
    if (enough) {
      r.band[b] = processBand(img, w, y0, y1, thr, prevPos_[b]);
    } else {
      r.band[b].y0 = y0;
      r.band[b].y1 = y1;
    }
    r.band[b].contrast = contrast;
    r.band[b].threshold = thr;
    if (r.band[b].found) prevPos_[b] = r.band[b].pos;
  }
  return r;
}

BandResult Detector::processBand(const uint8_t* img, int w, int y0, int y1, int thr,
                                 float prevPos) {
  BandResult br;
  br.y0 = y0;
  br.y1 = y1;
  const int rows = y1 - y0;
  const bool dark = cfg_.darkLine;

  // 1. 列ごとに「線の画素」を数える
  for (int x = 0; x < w; x++) profile_[x] = 0;
  for (int y = y0; y < y1; y++) {
    const uint8_t* row = img + y * w;
    for (int x = 0; x < w; x++) {
      bool isLine = dark ? (row[x] <= thr) : (row[x] > thr);
      profile_[x] += isLine;
    }
  }
  int minCount = (int)(cfg_.colFill * rows + 0.5f);
  if (minCount < 1) minCount = 1;

  // 2. 線の列が連続する区間を探す
  const float half = (w - 1) * 0.5f;
  int over = 0;
  float bestDist = 1e9f, bestPos = 0, bestWidth = 0;
  bool any = false;
  int x = 0;
  while (x < w) {
    if (profile_[x] < minCount) { x++; continue; }
    float sumW = 0, sumWX = 0;
    int start = x;
    while (x < w && profile_[x] >= minCount) {
      sumW += profile_[x];
      sumWX += (float)profile_[x] * x;
      x++;
    }
    int len = x - start;
    over += len;
    float pos = (sumWX / sumW - half) / half;   // -1..1 に正規化
    // 3. 前回位置に最も近いセグメントを採用 (分岐・ノイズ対策)
    float d = pos - prevPos;
    if (d < 0) d = -d;
    if (d < bestDist) {
      bestDist = d;
      bestPos = pos;
      bestWidth = (float)len / w;
      any = true;
    }
  }
  br.coverage = (float)over / w;
  if (!any) return br;
  br.found = true;
  br.pos = bestPos;
  br.width = bestWidth;
  return br;
}

}  // namespace line
