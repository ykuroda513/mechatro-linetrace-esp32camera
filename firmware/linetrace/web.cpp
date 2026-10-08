// =============================================================
//  web.cpp
// =============================================================
#include "web.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_http_server.h>
#include "config.h"
#include "params.h"
#include "web_page.h"

static httpd_handle_t server = nullptr;
static char hostName[32] = "";
static bool apMode = false;
static uint8_t* frameOut = nullptr;
static constexpr size_t FRAME_OUT_CAP = 4 + 160 * 120;   // ヘッダ + QQVGA

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
  char buf[1024];
  int k = snprintf(buf, sizeof(buf),
                   "{\"running\":%s,\"camera\":%s,\"fps\":%.1f,\"frame\":%lu,"
                   "\"net\":{\"mode\":\"%s\",\"host\":\"%s\",\"ip\":\"%s\",\"rssi\":%d},"
                   "\"ctrl\":{\"err\":%.4f,\"p\":%.4f,\"d\":%.4f,\"l\":%.3f,\"r\":%.3f,\"lost\":%s},"
                   "\"bands\":[",
                   shared::running() ? "true" : "false", shared::cameraOk() ? "true" : "false",
                   t.fps, (unsigned long)t.frameNo,
                   apMode ? "ap" : "sta", hostName,
                   (apMode ? WiFi.softAPIP() : WiFi.localIP()).toString().c_str(),
                   apMode ? 0 : (int)WiFi.RSSI(),
                   t.out.error, t.out.p, t.out.d, t.out.left, t.out.right,
                   t.out.lost ? "true" : "false");
  for (int i = 0; i < line::NUM_BANDS; i++) {
    const line::BandResult& b = t.det.band[i];
    k += snprintf(buf + k, sizeof(buf) - k,
                  "%s{\"found\":%s,\"pos\":%.4f,\"width\":%.3f,\"cov\":%.3f,"
                  "\"contrast\":%d,\"thr\":%d,\"y0\":%d,\"y1\":%d}",
                  i ? "," : "", b.found ? "true" : "false", b.pos, b.width, b.coverage,
                  b.contrast, b.threshold, b.y0, b.y1);
  }
  snprintf(buf + k, sizeof(buf) - k,
           "],\"enc\":{\"l\":%ld,\"r\":%ld,\"sl\":%.1f,\"sr\":%.1f,\"ppr\":%d},\"vm\":%.2f}",
           (long)t.encL, (long)t.encR, t.spdL, t.spdR, ENC_PULSES_PER_REV, t.vm);
  return sendJson(req, buf);
}

static esp_err_t hParams(httpd_req_t* req) {
  Params p = shared::getParams();
  char buf[2048];
  int k = snprintf(buf, sizeof(buf), "[");
  for (int i = 0; i < kParamCount; i++) {
    const ParamDesc& d = kParamTable[i];
    k += snprintf(buf + k, sizeof(buf) - k,
                  "%s{\"key\":\"%s\",\"label\":\"%s\",\"value\":%g,\"min\":%g,\"max\":%g,\"step\":%g}",
                  i ? "," : "", d.key, d.label, p.*(d.member), d.min, d.max, d.step);
  }
  snprintf(buf + k, sizeof(buf) - k, "]");
  return sendJson(req, buf);
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
    shared::setRunning(atoi(val) != 0);
  }
  return sendJson(req, shared::running() ? "{\"running\":true}" : "{\"running\":false}");
}

static esp_err_t hSave(httpd_req_t* req) {
  return sendJson(req, shared::saveParams() ? "{\"ok\":true}" : "{\"ok\":false}");
}

static esp_err_t hReset(httpd_req_t* req) {
  shared::resetParams();
  return sendJson(req, "{\"ok\":true}");
}

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

// 接続できないときはロボット自身がアクセスポイントになる
static void startAccessPoint(uint16_t id) {
  char ssid[32];
  snprintf(ssid, sizeof(ssid), "%s%04X", WIFI_AP_SSID_PREFIX, id);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, WIFI_AP_PASSWORD);
  WiFi.setSleep(false);
  apMode = true;
  Serial.printf("[wifi] AP mode  SSID: %s  PASS: %s\n", ssid, WIFI_AP_PASSWORD);
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

  if (!startStation()) startAccessPoint(id);

  // 再接続したときにもIPを表示
  WiFi.onEvent([](WiFiEvent_t, WiFiEventInfo_t) { printUrl(); },
               ARDUINO_EVENT_WIFI_STA_GOT_IP);

  if (MDNS.begin(hostName)) MDNS.addService("http", "tcp", 80);
  printUrl();

  frameOut = (uint8_t*)(psramFound() ? ps_malloc(FRAME_OUT_CAP) : malloc(FRAME_OUT_CAP));

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.max_uri_handlers = 12;
  cfg.stack_size = 8192;
  if (httpd_start(&server, &cfg) != ESP_OK) {
    Serial.println("[web] httpd_start failed");
    return;
  }
  struct { const char* uri; esp_err_t (*fn)(httpd_req_t*); } routes[] = {
    {"/", hIndex}, {"/frame", hFrame}, {"/status", hStatus}, {"/params", hParams},
    {"/set", hSet}, {"/run", hRun}, {"/save", hSave}, {"/reset", hReset},
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
