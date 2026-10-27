// =============================================================
//  web.cpp
// =============================================================
#include "web.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_http_server.h>
#include <stdarg.h>
#include "config.h"
#include "params.h"
#include "drive.h"
#include "web_page.h"

static httpd_handle_t server = nullptr;
static char hostName[32] = "";
static bool apMode = false;
static uint8_t* frameOut = nullptr;
static constexpr size_t FRAME_OUT_CAP = 4 + 160 * 120;   // ヘッダ + QQVGA

// 固定長バッファへの安全な追記 (あふれたら以降を書かず overflow を立てる)
struct JsonBuf {
  char* b;
  size_t cap;
  size_t len = 0;
  bool overflow = false;
  JsonBuf(char* buf, size_t c) : b(buf), cap(c) { b[0] = 0; }
  void add(const char* fmt, ...) __attribute__((format(printf, 2, 3))) {
    if (overflow) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(b + len, cap - len, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - len) { overflow = true; b[len] = 0; return; }
    len += n;
  }
};

static esp_err_t sendJson(httpd_req_t* req, const char* json) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_sendstr(req, json);
}

static esp_err_t hIndex(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  return httpd_resp_send(req, kIndexHtml, sizeof(kIndexHtml) - 1);
}

static esp_err_t hFrame(httpd_req_t* req) {
  int w = 0, h = 0;
  size_t n = shared::getFrame(frameOut + 4, FRAME_OUT_CAP - 4, &w, &h);
  if (n == 0) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no frame");
  frameOut[0] = w & 0xff; frameOut[1] = w >> 8;
  frameOut[2] = h & 0xff; frameOut[3] = h >> 8;
  httpd_resp_set_type(req, "application/octet-stream");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, (const char*)frameOut, n + 4);
}

static esp_err_t hStatus(httpd_req_t* req) {
  Telemetry t = shared::getTelemetry();
  drive::State ds = drive::state();
  char buf[1536];
  JsonBuf j(buf, sizeof(buf));
  j.add(
                   "{\"boot\":%lu,\"unsaved\":%s,\"running\":%s,\"camera\":%s,\"fps\":%.1f,\"frame\":%lu,"
                   "\"net\":{\"mode\":\"%s\",\"host\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
                   "\"ctrl\":{\"err\":%.4f,\"p\":%.4f,\"d\":%.4f,\"l\":%.3f,\"r\":%.3f,\"lost\":%s},"
                   "\"bands\":[",
                   (unsigned long)shared::bootId(), shared::unsaved() ? "true" : "false",
                   shared::running() ? "true" : "false", shared::cameraOk() ? "true" : "false",
                   t.fps, (unsigned long)t.frameNo,
                   apMode ? "ap" : "sta", hostName,
                   (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(),
                   apMode ? 0 : (int)WiFi.RSSI(),
                   t.out.error, t.out.p, t.out.d, t.out.left, t.out.right,
                   t.out.lost ? "true" : "false");
  for (int i = 0; i < line::NUM_BANDS; i++) {
    const line::BandResult& b = t.det.band[i];
    j.add(
                  "%s{\"found\":%s,\"pos\":%.4f,\"width\":%.3f,\"cov\":%.3f,"
                  "\"contrast\":%d,\"thr\":%d,\"y0\":%d,\"y1\":%d}",
                  i ? "," : "", b.found ? "true" : "false", b.pos, b.width, b.coverage,
                  b.contrast, b.threshold, b.y0, b.y1);
  }
  j.add(
           "],\"drv\":{\"active\":%s,\"test\":%s,\"calib\":%s,\"fb\":%s,\"vmax\":[%.0f,%.0f],"
           "\"tl\":%.0f,\"tr\":%.0f,\"sl\":%.0f,\"sr\":%.0f,\"dl\":%.3f,\"dr\":%.3f,"
           "\"cal\":[%.0f,%.0f]},"
           "\"enc\":{\"l\":%lu,\"r\":%lu,\"dl\":%lu,\"dr\":%lu,\"ppr\":%.1f,\"wheel\":%.1f},\"vm\":%.2f,"
           "\"luma\":{\"mean\":%d,\"max\":%d}}",
           ds.active ? "true" : "false", ds.testing ? "true" : "false",
           ds.calibrating ? "true" : "false", ds.feedback ? "true" : "false", ds.vmaxL, ds.vmaxR,
           ds.tgtL, ds.tgtR, ds.spdL, ds.spdR, ds.dutyL, ds.dutyR, ds.calibL, ds.calibR,
           (unsigned long)ds.encL, (unsigned long)ds.encR,
           (unsigned long)(ds.encL - ds.baseL), (unsigned long)(ds.encR - ds.baseR),
           (double)ENC_PULSES_PER_REV, (double)WHEEL_DIAMETER_MM,
           t.vm, t.lumaMean, t.lumaMax);
  if (j.overflow) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "status too long");
  return sendJson(req, buf);
}

// パラメータ一覧。項目が増えても溢れないよう1項目ずつ分割して送る
static esp_err_t hParams(httpd_req_t* req) {
  const Params p = shared::getParams();
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  char item[256];
  httpd_resp_send_chunk(req, "[", 1);
  for (int i = 0; i < kParamCount; i++) {
    const ParamDesc& d = kParamTable[i];
    int n = snprintf(item, sizeof(item),
                     "%s{\"key\":\"%s\",\"label\":\"%s\",\"value\":%g,\"min\":%g,\"max\":%g,\"step\":%g}",
                     i ? "," : "", d.key, d.label, p.*(d.member), d.min, d.max, d.step);
    if (n < 0 || n >= (int)sizeof(item)) continue;   // 長すぎる項目は飛ばす (起こらない想定)
    httpd_resp_send_chunk(req, item, n);
  }
  httpd_resp_send_chunk(req, "]", 1);
  return httpd_resp_send_chunk(req, nullptr, 0);
}

static esp_err_t hSet(httpd_req_t* req) {
  char q[256], val[32];
  int changed = 0;
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    for (int i = 0; i < kParamCount; i++) {
      if (httpd_query_key_value(q, kParamTable[i].key, val, sizeof(val)) == ESP_OK) {
        if (shared::setParam(kParamTable[i].key, atof(val))) changed++;
      }
    }
  }
  char res[48];
  snprintf(res, sizeof(res), "{\"changed\":%d}", changed);
  return sendJson(req, res);
}

static esp_err_t hRun(httpd_req_t* req) {
  char q[32], val[8];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
      httpd_query_key_value(q, "on", val, sizeof(val)) == ESP_OK) {
    if (atoi(val) != 0) shared::setRunning(true);
    else drive::stopAll();
  }
  return sendJson(req, shared::running() ? "{\"running\":true}" : "{\"running\":false}");
}

// /motor  モータテスト。引数なしならロボットに保存されたテスト設定 (t_l, t_r, t_ms, t_rev) で回す
//   /motor?l=200&r=200&ms=2000&fb=1[&rev=10] のように上書きも可 [mm/s]
//   rev>0: 各車輪がエンコーダで rev 回転分数えたら止める
static esp_err_t hMotor(httpd_req_t* req) {
  // 既定値はロボット側に保存されているテスト設定 (Webページの表示と同じ値)
  const Params p = shared::getParams();
  float l = p.testL, r = p.testR, revs = p.testRev;
  uint32_t ms = (uint32_t)p.testMs;
  drive::FbMode fb = drive::FB_PARAM;   // 速度FBはパラメータ「速度FB」に従う
  char q[128], v[16];
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {   // API用の上書き
    if (httpd_query_key_value(q, "l", v, sizeof(v)) == ESP_OK) l = atof(v);
    if (httpd_query_key_value(q, "r", v, sizeof(v)) == ESP_OK) r = atof(v);
    if (httpd_query_key_value(q, "ms", v, sizeof(v)) == ESP_OK) ms = atoi(v);
    if (httpd_query_key_value(q, "rev", v, sizeof(v)) == ESP_OK) revs = atof(v);
    if (httpd_query_key_value(q, "fb", v, sizeof(v)) == ESP_OK)
      fb = atoi(v) ? drive::FB_ON : drive::FB_OFF;
  }
  const float lim = 3000;   // [mm/s]
  l = l > lim ? lim : (l < -lim ? -lim : l);
  r = r > lim ? lim : (r < -lim ? -lim : r);
  if (revs > 0) ms = 10000;   // 回転数指定のときは最大10秒 (安全のため)
  drive::startTest(l, r, ms, fb, revs);
  char res[96];
  snprintf(res, sizeof(res), "{\"ok\":true,\"l\":%.0f,\"r\":%.0f,\"ms\":%lu,\"rev\":%.0f}",
           l, r, (unsigned long)ms, revs);
  return sendJson(req, res);
}

// /calib  duty 100% で1.5秒回して左右の最高速度 vmax を測り、パラメータに設定
static esp_err_t hEncMark(httpd_req_t* req) {
  drive::markEncoders();
  return sendJson(req, "{\"ok\":true}");
}

static esp_err_t hCalib(httpd_req_t* req) {
  drive::startCalib();
  return sendJson(req, "{\"ok\":true}");
}

static esp_err_t hStop(httpd_req_t* req) {
  drive::stopAll();
  return sendJson(req, "{\"ok\":true}");
}

// 直近3秒の目標/実測速度/duty (100Hz)
static esp_err_t hLog(httpd_req_t* req) {
  static char* buf = nullptr;
  constexpr size_t CAP = 16384;
  if (!buf) buf = (char*)(psramFound() ? ps_malloc(CAP) : malloc(CAP));
  if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no mem");
  drive::logJson(buf, CAP);
  return sendJson(req, buf);
}

static esp_err_t hSave(httpd_req_t* req) {
  return sendJson(req, shared::saveParams() ? "{\"ok\":true}" : "{\"ok\":false}");
}

static esp_err_t hReset(httpd_req_t* req) {
  shared::resetParams();
  return sendJson(req, "{\"ok\":true}");
}

#if WIFI_USE_STATION
// 既存のアクセスポイントに接続 (成功: true)
static bool startStation() {
  if (strlen(WIFI_STA_SSID) == 0) {
    Serial.println("[wifi] WIFI_STA_SSID 未設定 (wifi_secrets.h を作成してください)");
    return false;
  }
  WiFi.setHostname(hostName);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
  Serial.printf("[wifi] connecting to \"%s\" ", WIFI_STA_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < WIFI_STA_TIMEOUT_MS) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.printf("[wifi] connect failed (status=%d)\n", (int)WiFi.status());
    WiFi.disconnect(true);
    return false;
  }
  return true;
}
#endif

// 接続できないときはロボット自身がアクセスポイントになる
static void startAccessPoint(uint16_t id) {
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%s%04X", WIFI_AP_SSID_PREFIX, id);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL, 0, WIFI_AP_MAX_CLIENTS);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_TX_POWER);
  apMode = true;
  Serial.printf("[wifi] AP mode  SSID: %s  PASS: %s  ch%d\n", ssid, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL);
}

static void printUrl() {
  IPAddress ip = apMode ? WiFi.softAPIP() : WiFi.localIP();
  Serial.printf("[wifi] open http://%s/  or  http://%s.local/\n", ip.toString().c_str(), hostName);
}

void webBegin() {
#if WIFI_ENABLE
  // MACアドレス下位2バイトをロボットのIDにする (ラベルに書いておくと便利)
  uint64_t mac = ESP.getEfuseMac();
  uint16_t id = (uint16_t)(((mac >> 32) & 0xff) << 8 | ((mac >> 40) & 0xff));
  snprintf(hostName, sizeof(hostName), "%s%04x", WIFI_HOSTNAME_PREFIX, id);

#if WIFI_USE_STATION
  if (!startStation()) startAccessPoint(id);
#else
  startAccessPoint(id);
#endif

  // 再接続したときにもIPを表示
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { printUrl(); },
               ARDUINO_EVENT_WIFI_STA_GOT_IP);

  if (MDNS.begin(hostName)) MDNS.addService("http", "tcp", 80);
  printUrl();

  frameOut = (uint8_t*)(psramFound() ? ps_malloc(FRAME_OUT_CAP) : malloc(FRAME_OUT_CAP));

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 16;
  cfg.stack_size = 8192;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    Serial.println("[web] httpd_start failed");
    return;
  }
  struct { const char* uri; esp_err_t (*fn)(httpd_req_t*); } routes[] = {
    {"/", hIndex}, {"/frame", hFrame}, {"/status", hStatus}, {"/params", hParams},
    {"/set", hSet}, {"/run", hRun}, {"/save", hSave}, {"/reset", hReset},
    {"/motor", hMotor}, {"/stop", hStop}, {"/log", hLog}, {"/calib", hCalib}, {"/encmark", hEncMark},
  };
  for (auto& r : routes) {
    httpd_uri_t u = {};
    u.uri = r.uri;
    u.method = HTTP_GET;
    u.handler = r.fn;
    httpd_register_uri_handler(server, &u);
  }
#endif
}
