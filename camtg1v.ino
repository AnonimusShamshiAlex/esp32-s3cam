#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ════════════════════════════════════════════
const char* WIFI_SSID  = "ESP_Test";
const char* WIFI_PASS  = "1234567890";
const char* BOT_TOKEN  = "TG_TOKEN_BOT";
const char* CHAT_ID    = "TG_YOU_ID";
// ════════════════════════════════════════════

const unsigned long INTERVAL = 5000;

#define PWDN_GPIO_NUM  -1
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM  15
#define SIOD_GPIO_NUM  4
#define SIOC_GPIO_NUM  5
#define Y2_GPIO_NUM    11
#define Y3_GPIO_NUM    9
#define Y4_GPIO_NUM    8
#define Y5_GPIO_NUM    10
#define Y6_GPIO_NUM    12
#define Y7_GPIO_NUM    18
#define Y8_GPIO_NUM    17
#define Y9_GPIO_NUM    16
#define VSYNC_GPIO_NUM 6
#define HREF_GPIO_NUM  7
#define PCLK_GPIO_NUM  13

const char* TG_HOST = "api.telegram.org";
WiFiClientSecure client;
unsigned long lastSend = 0;
int photoNum = 0;

bool initCamera() {
  camera_config_t cfg;
  cfg.ledc_channel = LEDC_CHANNEL_0;
  cfg.ledc_timer   = LEDC_TIMER_0;
  cfg.pin_d0 = Y2_GPIO_NUM; cfg.pin_d1 = Y3_GPIO_NUM;
  cfg.pin_d2 = Y4_GPIO_NUM; cfg.pin_d3 = Y5_GPIO_NUM;
  cfg.pin_d4 = Y6_GPIO_NUM; cfg.pin_d5 = Y7_GPIO_NUM;
  cfg.pin_d6 = Y8_GPIO_NUM; cfg.pin_d7 = Y9_GPIO_NUM;
  cfg.pin_xclk = XCLK_GPIO_NUM; cfg.pin_pclk = PCLK_GPIO_NUM;
  cfg.pin_vsync = VSYNC_GPIO_NUM; cfg.pin_href = HREF_GPIO_NUM;
  cfg.pin_sscb_sda = SIOD_GPIO_NUM; cfg.pin_sscb_scl = SIOC_GPIO_NUM;
  cfg.pin_pwdn = PWDN_GPIO_NUM; cfg.pin_reset = RESET_GPIO_NUM;
  cfg.xclk_freq_hz = 20000000;
  cfg.pixel_format = PIXFORMAT_JPEG;
  cfg.frame_size   = FRAMESIZE_QVGA;
  cfg.jpeg_quality = 15;
  cfg.fb_count     = 1;
  cfg.fb_location  = CAMERA_FB_IN_DRAM;

  esp_err_t err = esp_camera_init(&cfg);
  if (err != ESP_OK) { Serial.printf("❌ Камера: 0x%x\n", err); return false; }
  Serial.println("✅ Камера OK");
  return true;
}

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  delay(500);
  Serial.println("\n🚀 ESP32-S3-CAM запускается...");

  if (!initCamera()) { delay(3000); ESP.restart(); }

  // ── WiFi точно как в ESPRadar v3.0 ──
  WiFi.mode(WIFI_AP_STA);          // 1. режим
  WiFi.begin(WIFI_SSID, WIFI_PASS); // 2. подключаемся к роутеру
  Serial.print("[WiFi] " + String(WIFI_SSID));
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n⚠️ Не подключился, статус: " + String(WiFi.status()));
  }
  // 3. AP после STA (как в ESPRadar)
  WiFi.softAP("ESP32CAM", "12345678", 6, 0, 4);
  Serial.println("[AP] ESP32CAM поднят");

  delay(1000);
  sendPhoto();
  lastSend = millis();
}

void sendPhoto() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi нет, статус: " + String(WiFi.status()));
    return;
  }
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) { Serial.println("❌ Кадр не получен"); return; }
  Serial.printf("📸 Фото #%d, %d байт\n", ++photoNum, fb->len);

  client.setInsecure();
  if (!client.connect(TG_HOST, 443)) {
    Serial.println("❌ Нет соединения с Telegram");
    esp_camera_fb_return(fb);
    return;
  }

  String boundary = "Boundary7MA4YWxkTrZu0gW";
  String url      = "/bot" + String(BOT_TOKEN) + "/sendPhoto";
  String head =
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"chat_id\"\r\n\r\n" +
    String(CHAT_ID) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"caption\"\r\n\r\n"
    "📷 Фото #" + String(photoNum) + "\r\n"
    "--" + boundary + "\r\n"
    "Content-Disposition: form-data; name=\"photo\"; filename=\"cam.jpg\"\r\n"
    "Content-Type: image/jpeg\r\n\r\n";
  String tail = "\r\n--" + boundary + "--\r\n";
  int total   = head.length() + fb->len + tail.length();

  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: " + String(TG_HOST));
  client.println("Content-Type: multipart/form-data; boundary=" + boundary);
  client.println("Content-Length: " + String(total));
  client.println("Connection: close");
  client.println();
  client.print(head);
  client.write(fb->buf, fb->len);
  client.print(tail);

  esp_camera_fb_return(fb); // Память сразу освобождена

  unsigned long timeout = millis() + 8000;
  while (!client.available()) {
    if (millis() > timeout) { Serial.println("❌ Таймаут"); client.stop(); return; }
    delay(50);
  }
  String resp = client.readStringUntil('\n');
  client.stop();
  if (resp.indexOf("200") > 0) Serial.println("✅ Отправлено!");
  else Serial.println("❌ Ошибка: " + resp);
}

void loop() {
  static unsigned long lastRecon = 0;
  if (WiFi.status() != WL_CONNECTED && millis() - lastRecon > 15000) {
    lastRecon = millis();
    Serial.println("🔄 Переподключаюсь...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }

  if (millis() - lastSend >= INTERVAL) {
    lastSend = millis();
    sendPhoto();
  }
}
