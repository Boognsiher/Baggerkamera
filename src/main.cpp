#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "camera_pins.h"

#if __has_include("secrets.h")
#include "secrets.h"
#else
#error "Copy include/secrets.h.example to include/secrets.h and fill in your WiFi details."
#endif

static httpd_handle_t streamServer = NULL;
static DNSServer dnsServer;
static bool apActive = false;

// ---- Camera -----------------------------------------------------------

static bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // VGA @ moderate quality keeps latency low, which matters more than
  // resolution when you're using this to steer a digger. Bump
  // frame_size up via /control if you want a sharper still picture
  // instead of smooth motion.
  if (psramFound()) {
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.grab_mode = CAMERA_GRAB_LATEST;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 15;
    config.fb_count = 1;
    config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor->id.PID == OV3660_PID) {
    sensor->set_vflip(sensor, 1);
    sensor->set_brightness(sensor, 1);
    sensor->set_saturation(sensor, -2);
  }
  return true;
}

// ---- HTTP handlers ------------------------------------------------------

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, user-scalable=no">
<title>Baggerkamera</title>
<style>
  html,body{margin:0;padding:0;background:#111;color:#eee;font-family:sans-serif;height:100%;}
  #wrap{display:flex;flex-direction:column;height:100%;}
  #stream{flex:1;width:100%;object-fit:contain;background:#000;}
  #bar{display:flex;gap:8px;padding:8px;background:#000;flex-wrap:wrap;justify-content:center;}
  button,select{padding:10px 14px;font-size:16px;border-radius:6px;border:none;background:#333;color:#eee;}
  button:active{background:#555;}
</style>
</head>
<body>
<div id="wrap">
  <img id="stream" src="/stream">
  <div id="bar">
    <button onclick="toggleLed()">Licht an/aus</button>
    <select id="fs" onchange="setVar('framesize', this.value)">
      <option value="6">QVGA (schnell)</option>
      <option value="8" selected>VGA</option>
      <option value="9">SVGA</option>
      <option value="10">XGA (scharf, langsamer)</option>
    </select>
  </div>
</div>
<script>
let ledOn = false;
function toggleLed(){
  ledOn = !ledOn;
  setVar('led', ledOn ? '1' : '0');
}
function setVar(name, value){
  fetch('/control?var=' + name + '&val=' + value);
}
</script>
</body>
</html>
)HTML";

static esp_err_t indexHandler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

#define PART_BOUNDARY "baggerkamera"
static const char *STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

static esp_err_t streamHandler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  char part_buf[64];
  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      res = ESP_FAIL;
    } else {
      size_t hlen = snprintf(part_buf, sizeof(part_buf), STREAM_PART, fb->len);
      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
      }
      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, part_buf, hlen);
      }
      if (res == ESP_OK) {
        res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
      }
      esp_camera_fb_return(fb);
    }
    if (res != ESP_OK) {
      break;
    }
  }
  return res;
}

static esp_err_t captureHandler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "image/jpeg");
  esp_err_t res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

static esp_err_t controlHandler(httpd_req_t *req) {
  char query[64];
  char var[16] = {0};
  char valBuf[16] = {0};

  if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK ||
      httpd_query_key_value(query, "var", var, sizeof(var)) != ESP_OK ||
      httpd_query_key_value(query, "val", valBuf, sizeof(valBuf)) != ESP_OK) {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  int val = atoi(valBuf);

  sensor_t *sensor = esp_camera_sensor_get();
  int res = 0;
  if (!strcmp(var, "framesize")) {
    res = sensor->set_framesize(sensor, (framesize_t)val);
  } else if (!strcmp(var, "quality")) {
    res = sensor->set_quality(sensor, val);
  } else if (!strcmp(var, "vflip")) {
    res = sensor->set_vflip(sensor, val);
  } else if (!strcmp(var, "hmirror")) {
    res = sensor->set_hmirror(sensor, val);
  } else if (!strcmp(var, "led")) {
    digitalWrite(LED_GPIO_NUM, val ? HIGH : LOW);
  } else {
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }

  if (res) {
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_uri_handlers = 8;

  httpd_uri_t indexUri = {"/", HTTP_GET, indexHandler, NULL};
  httpd_uri_t streamUri = {"/stream", HTTP_GET, streamHandler, NULL};
  httpd_uri_t captureUri = {"/capture", HTTP_GET, captureHandler, NULL};
  httpd_uri_t controlUri = {"/control", HTTP_GET, controlHandler, NULL};

  if (httpd_start(&streamServer, &config) == ESP_OK) {
    httpd_register_uri_handler(streamServer, &indexUri);
    httpd_register_uri_handler(streamServer, &streamUri);
    httpd_register_uri_handler(streamServer, &captureUri);
    httpd_register_uri_handler(streamServer, &controlUri);
  }
}

// ---- WiFi -----------------------------------------------------------

static void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(WIFI_AP_SSID, strlen(WIFI_AP_PASSWORD) ? WIFI_AP_PASSWORD : NULL);
  IPAddress ip = WiFi.softAPIP();
  Serial.printf("Hotspot \"%s\" aktiv. Mit dem Handy verbinden und http://%s/ oeffnen.\n",
                WIFI_AP_SSID, ip.toString().c_str());

  // Redirect every DNS lookup from a connected phone to our own IP, so
  // opening almost any URL (or the OS's captive-portal check) lands on
  // the camera page automatically.
  dnsServer.start(53, "*", ip);
  apActive = true;
}

static bool connectToStation(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASSWORD);
  Serial.printf("Verbinde mit WLAN \"%s\"...\n", WIFI_STA_SSID);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Verbunden. Kamera erreichbar unter http://%s/\n",
                  WiFi.localIP().toString().c_str());
    if (MDNS.begin(MDNS_HOSTNAME)) {
      Serial.printf("Auch erreichbar unter http://%s.local/\n", MDNS_HOSTNAME);
    }
    return true;
  }
  return false;
}

static void setupWifi() {
#if WIFI_MODE == WIFI_MODE_AP_ONLY
  startAccessPoint();
#elif WIFI_MODE == WIFI_MODE_STA_ONLY
  if (!connectToStation(20000)) {
    Serial.println("WLAN-Verbindung fehlgeschlagen. Neustart in 5s...");
    delay(5000);
    ESP.restart();
  }
#else
  if (!connectToStation(15000)) {
    Serial.println("Heim-WLAN nicht erreichbar, starte eigenen Hotspot als Fallback.");
    startAccessPoint();
  }
#endif
}

// ---- Arduino entry points ------------------------------------------------

void setup() {
  // ESP32-CAM boards commonly brown out under the camera's inrush current
  // on marginal 5V supplies; this disables the detector so a brief dip
  // doesn't reset the board. It's a workaround, not a fix for bad power -
  // use a solid 5V/2A supply if you see repeated random reboots.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  Serial.setDebugOutput(false);

  pinMode(LED_GPIO_NUM, OUTPUT);
  digitalWrite(LED_GPIO_NUM, LOW);

  if (!initCamera()) {
    Serial.println("Kamera-Initialisierung fehlgeschlagen. Verkabelung pruefen.");
  }

  setupWifi();
  startCameraServer();
}

void loop() {
  if (apActive) {
    dnsServer.processNextRequest();
  }
  delay(2);
}
