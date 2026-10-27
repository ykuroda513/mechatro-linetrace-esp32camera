// =============================================================
//  PC上で画像処理・制御のロジックをテストする
//    cd test && make
// =============================================================
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "line_detector.h"
#include "controller.h"
#include "speed_ctrl.h"

static int fails = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) { std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); fails++; } \
  } while (0)

constexpr int W = 160, H = 120;

// 背景 bg に、中心 cx・幅 lw の縦線 (輝度 fg) を描く。ノイズ付き
static std::vector<uint8_t> makeImage(std::vector<float> centers, int lw, uint8_t bg, uint8_t fg,
                                      unsigned seed = 1) {
  std::vector<uint8_t> img(W * H);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++) {
      seed = seed * 1103515245u + 12345u;
      int noise = (int)((seed >> 16) % 11) - 5;
      int v = bg;
      for (float c : centers)
        if (std::fabs(x - c) <= lw / 2.0f) v = fg;
      v += noise;
      img[y * W + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
  return img;
}

static float expectedPos(float cx) { return (cx - (W - 1) * 0.5f) / ((W - 1) * 0.5f); }

static void testCenter() {
  std::puts("center line");
  line::Detector d;
  auto img = makeImage({79.5f}, 12, 200, 40);
  auto r = d.process(img.data(), W, H);
  for (auto& b : r.band) {
    CHECK(b.found);
    CHECK(std::fabs(b.pos) < 0.03f);
    CHECK(std::fabs(b.width - 13.0f / W) < 0.03f);
  }
}

static void testOffset() {
  std::puts("offset line");
  line::Detector d;
  auto img = makeImage({120.0f}, 10, 210, 30);
  auto r = d.process(img.data(), W, H);
  CHECK(r.band[0].found);
  CHECK(std::fabs(r.band[0].pos - expectedPos(120)) < 0.03f);
  CHECK(r.band[0].pos > 0);  // 右側は正
}

static void testNoLine() {
  std::puts("no line");
  line::Detector d;
  auto img = makeImage({}, 0, 180, 0);
  auto r = d.process(img.data(), W, H);
  for (auto& b : r.band) CHECK(!b.found);
}

static void testWhiteLine() {
  std::puts("white line on black");
  line::Detector d;
  line::Config c;
  c.darkLine = false;
  d.setConfig(c);
  auto img = makeImage({40.0f}, 10, 30, 220);
  auto r = d.process(img.data(), W, H);
  CHECK(r.band[0].found);
  CHECK(std::fabs(r.band[0].pos - expectedPos(40)) < 0.03f);
  // 黒線設定のままだと背景を線と誤認するので、位置が合わないはず
  line::Detector d2;
  auto r2 = d2.process(img.data(), W, H);
  CHECK(std::fabs(r2.band[0].pos - expectedPos(40)) > 0.2f);
}

static void testTracking() {
  std::puts("two lines: keep tracking the nearer one");
  line::Detector d;
  auto first = makeImage({50.0f}, 10, 200, 40);
  d.process(first.data(), W, H);  // 前回位置 = 左側
  auto both = makeImage({55.0f, 130.0f}, 10, 200, 40);
  auto r = d.process(both.data(), W, H);
  CHECK(r.band[0].found);
  CHECK(std::fabs(r.band[0].pos - expectedPos(55)) < 0.03f);
  CHECK(r.band[0].coverage > 0.10f);  // 2本ぶんの被覆率
}

static void testCrossing() {
  std::puts("crossing line: high coverage");
  line::Detector d;
  // 縦線 + 手前の帯にかかる横線 (交差点)
  std::vector<uint8_t> img2(W * H, 200);
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if ((x >= 75 && x < 85) || (y >= 95 && y < 105)) img2[y * W + x] = 30;
  auto r = d.process(img2.data(), W, H);
  CHECK(r.band[0].found);
  CHECK(r.band[0].coverage > 0.9f);
  CHECK(r.band[1].coverage < 0.15f);
}

static line::Result makeResult(float nearPos, bool nearFound = true, float farPos = 0,
                               bool farFound = false) {
  line::Result r;
  r.band[0].found = nearFound;
  r.band[0].pos = nearPos;
  r.band[2].found = farFound;
  r.band[2].pos = farPos;
  return r;
}

static void testController() {
  std::puts("controller");
  ctrl::LineController c;
  ctrl::Gains g;
  g.kp = 1.0f; g.kd = 0.0f; g.baseSpeed = 0.5f; g.maxSpeed = 0.9f; g.curveSlow = 0; g.lookahead = 0;
  c.setGains(g);

  auto o = c.update(makeResult(0.0f), 0.02f);
  CHECK(std::fabs(o.left - 0.5f) < 1e-5f && std::fabs(o.right - 0.5f) < 1e-5f);

  o = c.update(makeResult(0.2f), 0.02f);  // 線が右 → 右旋回
  CHECK(o.left > o.right);
  CHECK(std::fabs(o.left - 0.7f) < 1e-5f && std::fabs(o.right - 0.3f) < 1e-5f);

  o = c.update(makeResult(1.0f), 0.02f);  // 上限クリップ: 差を保ってスケール
  CHECK(std::fabs(std::fmax(std::fabs(o.left), std::fabs(o.right)) - 0.9f) < 1e-5f);
  CHECK(o.left > 0 && o.right < 0);

  o = c.update(makeResult(0, false), 0.02f);  // 見失い → 最後の右側へ旋回
  CHECK(o.lost);
  CHECK(o.left > 0 && o.right < 0);

  // 先読み: 手前0, 遠方-1, lookahead 0.5 → 偏差 -0.5
  c.reset();
  g.lookahead = 0.5f;
  c.setGains(g);
  o = c.update(makeResult(0.0f, true, -1.0f, true), 0.02f);
  CHECK(std::fabs(o.error + 0.5f) < 1e-5f);

  // 微分: 偏差が増える方向に D 項が働く
  c.reset();
  g.kp = 0; g.kd = 0.1f; g.lookahead = 0; g.dFilter = 0;
  c.setGains(g);
  c.update(makeResult(0.0f), 0.02f);
  o = c.update(makeResult(0.1f), 0.02f);
  CHECK(std::fabs(o.d - 0.1f * 0.1f / 0.02f) < 1e-4f);
}

// 1次遅れのモータモデル: 速度 y' = (K*duty - y)/tau
//   K < 1 は「フィードフォワードの見込みより遅い」(電池消耗・負荷) を表す
static float simulate(spd::SpeedPI& pi, float target, float K, float tau, float T,
                      float* maxDuty = nullptr) {
  const float dt = 0.01f;
  float y = 0;
  if (maxDuty) *maxDuty = 0;
  for (float t = 0; t < T; t += dt) {
    float u = pi.update(target, y, dt);
    if (maxDuty && std::fabs(u) > *maxDuty) *maxDuty = std::fabs(u);
    y += (K * u - y) / tau * dt;
  }
  return y;
}

static void testSpeedPI() {
  std::puts("speed PI");
  spd::Gains g;  // 既定 kp=0.5 ki=4 ff=1
  spd::SpeedPI pi;
  pi.setGains(g);

  // モデルが見込みどおり (K=1): 目標に一致
  float y = simulate(pi, 0.5f, 1.0f, 0.08f, 1.5f);
  CHECK(std::fabs(y - 0.5f) < 0.01f);

  // 2割遅いモータ (K=0.8): FFだけだと 0.4 止まりだが、積分で 0.5 に追従
  pi.reset();
  y = simulate(pi, 0.5f, 0.8f, 0.08f, 2.0f);
  CHECK(std::fabs(y - 0.5f) < 0.01f);

  // 逆転も同様
  pi.reset();
  y = simulate(pi, -0.4f, 0.8f, 0.08f, 2.0f);
  CHECK(std::fabs(y + 0.4f) < 0.01f);

  // 到達不能な目標: duty は ±1 に飽和し、積分は上限で止まる (ワインドアップしない)
  pi.reset();
  float md = 0;
  y = simulate(pi, 1.0f, 0.5f, 0.08f, 2.0f, &md);
  CHECK(md <= 1.0f + 1e-6f);
  CHECK(pi.integral() <= g.iLimit + 1e-6f);
  CHECK(std::fabs(y - 0.5f) < 0.02f);

  // 左右の個体差: 片側だけ 3割遅いモータでも 1.5 秒以内に目標の ±2% に入る
  pi.reset();
  y = simulate(pi, 0.33f, 0.7f, 0.08f, 1.5f);
  CHECK(std::fabs(y - 0.33f) < 0.33f * 0.02f);

  // FF が大きく外れていて、積分項で duty を 0.6 以上補う必要がある場合でも定常偏差なし
  {
    spd::Gains g2;
    g2.ff = 0.3f;
    spd::SpeedPI pi2;
    pi2.setGains(g2);
    float y2 = simulate(pi2, 0.4f, 0.5f, 0.08f, 3.0f);   // 必要duty 0.8, FF分 0.12
    CHECK(std::fabs(y2 - 0.4f) < 0.01f);
    CHECK(pi2.integral() > 0.6f);
  }

  // 純粋なFB (ff=0) でも定常偏差なし
  g.ff = 0;
  pi.setGains(g);
  pi.reset();
  y = simulate(pi, 0.3f, 1.0f, 0.08f, 3.0f);
  CHECK(std::fabs(y - 0.3f) < 0.01f);
}

int main() {
  testCenter();
  testOffset();
  testNoLine();
  testWhiteLine();
  testTracking();
  testCrossing();
  testController();
  testSpeedPI();
  if (fails) { std::printf("\n%d check(s) FAILED\n", fails); return 1; }
  std::puts("\nall tests passed");
  return 0;
}
