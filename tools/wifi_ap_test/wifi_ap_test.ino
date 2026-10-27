// =============================================================
//  wifi_ap_test.ino  Wi-Fi アクセスポイントだけを動かす切り分け用スケッチ
//
//  カメラ・モータ・エンコーダを一切使わず、APとWebページだけを起動します。
//   - これで接続できる   → 本体ファームウェア側(カメラ等との組み合わせ)の問題
//   - これでも接続できない → アンテナ・電源・基板など、ハードウェアの問題
//  シリアルモニタ(115200bps)に、リセット理由と接続台数を表示します。
// =============================================================
#include <WiFi.h>
#include <WebServer.h>
#include <esp_system.h>

#define AP_SSID  "APTEST-XIAO"
#define AP_PASS  "mechatro"
#define TX_POWER WIFI_POWER_11dBm   // 接続時にリセットするなら 8_5dBm などに下げる

WebServer server(80);

void setup() {
  Serial.begin(115200);
  for (uint32_t t0 = millis(); !Serial && millis() - t0 < 2000;) delay(10);
  Serial.printf("\nreset reason = %d  (1=POWERON 3=SW 4=PANIC 9=BROWNOUT)\n",
                (int)esp_reset_reason());

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS, 1);
  WiFi.setTxPower(TX_POWER);
  Serial.printf("AP: %s / %s  http://%s/\n", AP_SSID, AP_PASS,
                WiFi.softAPIP().toString().c_str());

  server.on("/", [] {
    server.send(200, "text/html; charset=utf-8",
                "<h1>XIAO ESP32S3 AP OK</h1><p>uptime " + String(millis() / 1000) + " s</p>");
  });
  server.begin();
}

void loop() {
  server.handleClient();
  static uint32_t last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.printf("uptime %lus  clients %d\n", millis() / 1000,
                  WiFi.softAPgetStationNum());
  }
}
