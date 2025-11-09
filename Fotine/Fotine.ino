/*
  Cornice fotografica per ESP32-S3 4848S040.
  Autore: Davide Nasato (gat)
  Funzioni principali (immutate):
  - Pilotaggio pannello ST7701 480×480 via Arduino_GFX (type9).
  - Wi-Fi: STA da NVS, fallback AP con captive portal.
  - NTP per data/ora visualizzate nella barra in alto.
  - SD su HSPI: salvataggio e lettura immagini.
  - Download immagini via proxy (JPEG baseline), controllo SOF0.
  - Cambio immagine ogni 60 s.
*/

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino_GFX_Library.h>
#include <time.h>
#include <TJpg_Decoder.h>
#include <SPI.h>
#include <SD.h>

// ----------------------------- Retroilluminazione LCD -----------------------------
#define GFX_BL      38
#define PWM_CHANNEL 0
#define PWM_FREQ    1000
#define PWM_BITS    8

// ----------------------------- NTP / Fuso orario -----------------------------
// CET (+1h) e DST (+1h) per area CH/IT
static const char* NTP_SERVER = "pool.ntp.org";
static const long   GMT_OFFSET_SEC      = 3600;
static const int    DAYLIGHT_OFFSET_SEC = 3600;
static bool g_timeSynced = false;

// ----------------------------- Sorgente immagini (proxy) -----------------------------
// Il proxy forza JPEG baseline 480×480, qualità 85, con redirect gestiti da HTTPClient.
static const char* IMAGE_PROXY_BASE =
  "https://images.weserv.nl/"
  "?url=picsum.photos/480/480.jpg"
  "&output=jpg"
  "&quality=85"
  "&fit=cover"
  "&we=1&af=1"
  "&il=0";

// Intervallo di visualizzazione immagine (ms) — 300000 = 5 minuti
static const uint32_t IMAGE_DISPLAY_TIME = 300000;
static uint32_t lastImageChange = 0;

// ----------------------------- SD (HSPI) -----------------------------
// Pinout HSPI dedicato alla microSD del modulo
#define SD_CS_PIN   42
#define SD_MOSI_PIN 47
#define SD_CLK_PIN  48
#define SD_MISO_PIN 41
#define SD_SPI_FREQ 10000000UL

SPIClass spiSD(HSPI);
static bool sd_ok = false;
static const char* SD_JPEG_PATH = "/picsum.jpg";

// Hot-plug SD: rilevazione inserimento/estrazione
bool sdCardPresent = false;
uint32_t lastSDCheckTime = 0;
const uint32_t SD_CHECK_INTERVAL = 500;   // ms
uint32_t sdReinitTime = 0;
const uint32_t SD_REINIT_DELAY = 2000;    // ms

// ----------------------------- Bus & Pannello LCD -----------------------------
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED // DC, SCK, MOSI, MISO, CS
);

// Parametri del pannello RGB e timing per ST7701 (type9)
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  18, 17, 16, 21,      // DE, VSYNC, HSYNC, PCLK
  11, 12, 13, 14, 0,   // R0..R4
  8, 20, 3, 46, 9, 10, // G0..G5
  4, 5, 6, 7, 15,      // B0..B4
  1, 10, 8, 50,        // HSYNC params
  1, 10, 8, 20,        // VSYNC params
  0, 12000000, false, 0, 0, 0
);

// Display 480×480 con sequenza init type9 (fornita dalla libreria)
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480, rgbpanel, 0, true,
  bus, GFX_NOT_DEFINED,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// ----------------------------- Rete / Captive portal -----------------------------
DNSServer dnsServer;
WebServer web(80);
Preferences prefs;
String sta_ssid, sta_pass;
String ap_ssid, ap_pass;
const byte DNS_PORT = 53;
bool isAPMode = false;

// ----------------------------- Colori / Header UI -----------------------------
#define RGB565_ORANGE 0xFD20
#define RGB565_BLACK  0x0000
#define RGB565_WHITE  0xFFFF
static const int HEADER_H = 56;

// ----------------------------- Utils LCD -----------------------------
// PWM a 8 bit per accendere il backlight al 100%
static void backlightOn() {
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 255);
}

// Sequenza di avvio pannello + pulizia schermo
static void panelKickstart() {
  delay(50);
  gfx->begin();
  gfx->setRotation(0);
  delay(120);
  gfx->displayOn();
  delay(20);
  gfx->fillScreen(RGB565_BLACK);
  gfx->fillRect(0, 0, 480, 20, RGB565_ORANGE);
  delay(60);
}

// ----------------------------- NTP helpers -----------------------------
static bool waitForValidTime(uint32_t timeoutMs = 8000) {
  uint32_t t0 = millis();
  time_t now = 0; struct tm info;
  while ((millis() - t0) < timeoutMs) {
    time(&now); localtime_r(&now, &info);
    if (info.tm_year + 1900 > 2020) return true;
    delay(200);
  }
  return false;
}

// Sincronizza una sola volta con NTP
static void syncTimeOnce() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  g_timeSynced = waitForValidTime();
  Serial.println(g_timeSynced ? "NTP sync OK" : "NTP sync failed");
}

// Ritorna “gg/mm - hh:mm” se l’orologio è valido, altrimenti stringa vuota
static String getFormattedDateTime() {
  if (!g_timeSynced) return "";
  time_t now; struct tm timeinfo;
  time(&now); localtime_r(&now, &timeinfo);
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%02d/%02d - %02d:%02d",
           timeinfo.tm_mday, timeinfo.tm_mon + 1,
           timeinfo.tm_hour, timeinfo.tm_min);
  return String(buffer);
}

// Disegna header (barra arancione) e timestamp allineato a destra con lieve “ombra”
static void drawHeader() {
  gfx->fillRect(0, 0, 480, HEADER_H, RGB565_ORANGE);
  String datetime = getFormattedDateTime();
  if (datetime.length() > 0) {
    int textWidth = datetime.length() * 12;   // stima grezza larghezza testo
    int xPos = 480 - textWidth - 16;
    gfx->setTextSize(2);
    gfx->setTextColor(RGB565_BLACK, RGB565_ORANGE);
    gfx->setCursor(xPos+1, 28);   gfx->print(datetime);
    gfx->setCursor(xPos,   29);   gfx->print(datetime);
    gfx->setCursor(xPos+1, 29);   gfx->print(datetime);
    gfx->setCursor(xPos,   28);   gfx->print(datetime);
  }
}

// ----------------------------- Captive portal -----------------------------
static String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

// HTML minimale per inserimento SSID/password
static String htmlIndex() {
  String ip = ipToString(WiFi.softAPIP());
  String page =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Wi-Fi Setup</title>"
    "<style>body{font-family:sans-serif;background:#111;color:#eee;padding:24px}"
    "input,button{font-size:18px;padding:10px;margin:6px;width:100%;box-sizing:border-box}"
    "form{max-width:420px;margin:0 auto}</style></head><body>"
    "<h2>Configura Wi-Fi</h2>"
    "<form method='POST' action='/save'>"
    "<label>SSID</label><input name='ssid' required>"
    "<label>Password</label><input type='password' name='pass'>"
    "<button>Salva</button></form>"
    "<p>Se il popup non compare, apri <b>http://" + ip + "/</b></p>"
    "</body></html>";
  return page;
}

// Handlers HTTP del captive
static void handleRoot() { web.send(200, "text/html", htmlIndex()); }

static void handleSave() {
  if (web.hasArg("ssid") && web.hasArg("pass")) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    prefs.end();
    web.send(200, "text/html",
      "<html><body><h3>Salvate. Mi connetto...</h3>"
      "<script>setTimeout(()=>{fetch('/reboot')},800);</script>"
      "</body></html>");
  } else {
    web.send(400, "text/plain", "Bad Request");
  }
}

static void handleReboot() { web.send(200, "text/plain", "OK"); delay(100); ESP.restart(); }

// Schermata locale con SSID/password dell’AP di configurazione
static void drawAPScreen(const String& ssid, const String& pass) {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
  gfx->setCursor(16, 36);  gfx->print("Connettiti all'AP:");
  gfx->setCursor(16, 66);  gfx->print(ssid);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(16, 96);  gfx->print("Password: "); gfx->print(pass);
  gfx->setCursor(16, 114); gfx->print("Captive portal automatico.");
  gfx->setCursor(16, 126); gfx->print("Se non compare, apri l'IP dell'AP.");
}

// Avvio AP + DNS hijack + web server captive
static void startAPWithPortal() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssidbuf[32]; snprintf(ssidbuf, sizeof(ssidbuf), "PANEL-%02X%02X", mac[4], mac[5]);
  ap_ssid = ssidbuf; ap_pass = "panelsetup";

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
  delay(100);

  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
  web.on("/", HTTP_GET, handleRoot);
  web.on("/save", HTTP_POST, handleSave);
  web.on("/reboot", HTTP_GET, handleReboot);
  web.onNotFound(handleRoot);
  web.begin();

  drawAPScreen(ap_ssid, ap_pass);
  isAPMode = true;
}

// Tentativo di connessione STA usando credenziali persistite
static bool tryConnectSTA(uint32_t timeoutMs = 8000) {
  prefs.begin("wifi", true);
  sta_ssid = prefs.getString("ssid", "");
  sta_pass = prefs.getString("pass", "");
  prefs.end();
  if (sta_ssid.isEmpty()) return false;

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());

  uint32_t t0 = millis();
  while ((millis() - t0) < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return (WiFi.status() == WL_CONNECTED);
}

// ----------------------------- SD: init e monitor -----------------------------
// Inizializza la SD su HSPI, stampa dimensione e frequenza effective
static bool initSD() {
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  spiSD.begin(SD_CLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  delay(10);

  if (!SD.begin(SD_CS_PIN, spiSD, SD_SPI_FREQ)) {
    sd_ok = false; sdCardPresent = false;
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    sd_ok = false; sdCardPresent = false;
    return false;
  }

  uint64_t mb = SD.cardSize() / (1024ULL * 1024ULL);
  Serial.printf("[SD] OK (HSPI) — %llu MB @ %lu Hz\n",
                (unsigned long long)mb, (unsigned long)SD_SPI_FREQ);
  sd_ok = true; sdCardPresent = true;
  return true;
}

// Verifica rapida presenza SD (utile quando sd_ok è ancora falso)
static bool isSDCurrentlyPresent() {
  if (!sd_ok) {
    File root = SD.open("/");
    if (!root) return false;
    root.close();
    return true;
  }
  return (SD.cardType() != CARD_NONE);
}

// ----------------------------- Decoder JPEG -----------------------------
static bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y < HEADER_H) {
    int16_t skipRows = HEADER_H - y;
    if (skipRows >= (int16_t)h) return true;
    bitmap += skipRows * w; h -= skipRows; y = HEADER_H;
  }
  if (y >= 480 || x >= 480) return true;
  if (x + w > 480) w = 480 - x;
  if (y + h > 480) h = 480 - y;
  gfx->draw16bitRGBBitmap(x, y, bitmap, w, h);
  return true;
}

// ----------------------------- Download → SD -----------------------------
static bool writeStreamToSD_sniffJPEG(const char* path, WiFiClient* stream, int content_len) {
  File f = SD.open(path, FILE_WRITE);
  if (!f) { Serial.println("[SD] open write FAILED"); return false; }

  const size_t BUF = 2048;
  uint8_t buf[BUF];
  size_t total = 0;
  uint32_t t0 = millis();

  bool checkedMagic = false;
  bool jpegOK = false;

  while ((content_len < 0) || (total < (size_t)content_len)) {
    int avail = stream->available();
    if (avail <= 0) {
      if (!stream->connected()) break;
      delay(1);
      continue;
    }

    int toRead = avail > (int)BUF ? (int)BUF : avail;
    int n = stream->readBytes(buf, toRead);
    if (n <= 0) break;

    if (!checkedMagic) {
      checkedMagic = true;
      if (n >= 2 && buf[0] == 0xFF && buf[1] == 0xD8) jpegOK = true;
      if (!jpegOK) {
        Serial.println("[SD] Non è un JPEG (no 0xFFD8)");
        f.close(); SD.remove(path); return false;
      }
    }

    f.write(buf, n);
    total += n;
  }

  f.flush(); f.close();
  Serial.printf("[SD] scritto %u bytes in %u ms\n", (unsigned)total, (unsigned)(millis() - t0));
  return total > 0;
}

static bool downloadJPEGToSD() {
  String url = String(IMAGE_PROXY_BASE) + "&t=" + String(millis()); // cache-buster

  WiFiClientSecure client; client.setInsecure();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);
  http.addHeader("User-Agent", "ESP32");
  http.addHeader("Accept", "image/jpeg,image/*;q=0.8");

  const char* keys[] = {"Content-Type","Content-Length","Transfer-Encoding"};
  http.collectHeaders(keys, sizeof(keys)/sizeof(keys[0]));

  if (!http.begin(client, url)) {
    Serial.println("[HTTP] begin() FAILED");
    return false;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[HTTP] error: %d\n", httpCode);
    http.end();
    return false;
  }

  String ctype = http.header("Content-Type"); ctype.toLowerCase();
  int len = http.getSize(); // -1 se chunked
  Serial.printf("[HTTP] CT='%s' LEN=%d\n", ctype.c_str(), len);

  if (ctype.indexOf("jpeg") < 0) {
    Serial.println("[HTTP] Non JPEG, stop");
    http.end();
    return false;
  }

  WiFiClient* stream = http.getStreamPtr();
  bool ok = writeStreamToSD_sniffJPEG(SD_JPEG_PATH, stream, len);
  http.end();
  return ok;
}

// ----------------------------- Validazione JPEG baseline -----------------------------
static bool isJpegBaselineOnSD(const char* path) {
  File f = SD.open(path, FILE_READ);
  if (!f) { Serial.println("[SD] open read FAILED"); return false; }

  bool baseline = false, progressive = false;
  int prev = -1;
  while (f.available()) {
    int b = f.read();
    if (prev == 0xFF) {
      if (b == 0xC0) { baseline = true; break; }   // SOF0
      if (b == 0xC2) { progressive = true; break; } // SOF2
    }
    prev = b;
  }
  f.close();

  if (progressive) { Serial.println("[JPEG] PROGRESSIVE (SOF2) — skip"); return false; }
  if (!baseline)    { Serial.println("[JPEG] SOF0 non trovato — skip");   return false; }
  return true;
}

// ----------------------------- Rendering JPEG da SD -----------------------------
static bool drawJPEGFromSD() {

  // Decoder configurato per RGB565 nativi e scala 1:1
  TJpgDec.setSwapBytes(false);
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);

  if (!SD.exists(SD_JPEG_PATH)) {
    Serial.println("JPEG non presente su SD");
    return false;
  }
  int16_t res = TJpgDec.drawSdJpg(0, HEADER_H, SD_JPEG_PATH);
  if (res != 1) {
    Serial.printf("JPEG decode error: %d\n", (int)res);
    return false;
  }
  return true;
}

// ----------------------------- Pipeline: download → valida → disegna -----------------------------
static bool downloadAndShowFromSD() {
  if (!sd_ok || !sdCardPresent) {
    Serial.println("[FLOW] SD non pronta");
    return false;
  }

  if (!downloadJPEGToSD()) {
    Serial.println("[FLOW] download FAILED");
    return false;
  }
  if (!isJpegBaselineOnSD(SD_JPEG_PATH)) {
    SD.remove(SD_JPEG_PATH);
    delay(50);
    Serial.println("[FLOW] non baseline, scarto");
    return false;
  }
  return drawJPEGFromSD();
}

static uint32_t lastHeaderUpdate = 0;

// ----------------------------- Arduino: setup/loop -----------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Decoder impostato su endianess nativa (niente swap)
  TJpgDec.setSwapBytes(false);

  backlightOn();
  panelKickstart();

  // Splash di avvio
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(10, 10);  gfx->println("FOTINE - AVVIO");
  gfx->setCursor(10, 40);  gfx->print("WiFi connecting...");

  // Wi-Fi: tenta STA, altrimenti AP con captive
  if (!tryConnectSTA()) {
    gfx->setCursor(10, 70);
    gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
    gfx->println("WiFi FAILED - AP MODE");
    delay(3000);
    startAPWithPortal();
  } else {
    gfx->setCursor(10, 70);
    gfx->setTextColor(0x07E0, RGB565_BLACK);
    gfx->println("WiFi OK!");
    gfx->setCursor(10, 100);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->print("IP: "); gfx->println(WiFi.localIP());
    gfx->setCursor(10, 130);
    gfx->print("NTP sync..."); syncTimeOnce();
    gfx->setCursor(10, 160);
    if (g_timeSynced) { gfx->setTextColor(0x07E0, RGB565_BLACK); gfx->println("NTP OK!"); }
    else              { gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK); gfx->println("NTP FAILED"); }
  }

  // SD init + stato
  gfx->setCursor(10, 190);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->print("SD init...");
  sd_ok = initSD();
  gfx->setCursor(10, 210);
  if (sd_ok) { gfx->setTextColor(0x07E0, RGB565_BLACK); gfx->println("SD OK!"); }
  else       { gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK); gfx->println("SD FAILED"); }
  sdCardPresent = sd_ok;

  lastHeaderUpdate = millis();
  lastImageChange  = millis();

  if (!isAPMode && WiFi.status() == WL_CONNECTED && sd_ok && sdCardPresent) {
    (void)downloadAndShowFromSD(); 
  }

  drawHeader();
}

void loop() {
  uint32_t now = millis();

  // Hot-plug SD: rileva cambi e re-inizializza con ritardo per stabilizzare i contatti
  if (now - lastSDCheckTime >= SD_CHECK_INTERVAL) {
    lastSDCheckTime = now;
    bool presentNow = isSDCurrentlyPresent();
    if (sdCardPresent && !presentNow) {
      sdCardPresent = false; sd_ok = false; sdReinitTime = now + SD_REINIT_DELAY;
    }
    if (!sdCardPresent && presentNow) {
      sdCardPresent = true; sd_ok = false; sdReinitTime = now + SD_REINIT_DELAY;
    }
    if (sdCardPresent && !sd_ok && (int32_t)(now - sdReinitTime) >= 0) {
      if (initSD()) Serial.println("[SD] Re-init OK");
      else          { Serial.println("[SD] Re-init FALLITA"); sdReinitTime = now + SD_REINIT_DELAY; }
    }
  }

  // In AP mode gestisce DNS e HTTP, poi esce per ridurre il carico
  if (isAPMode) { dnsServer.processNextRequest(); web.handleClient(); delay(5); return; }

  // Aggiornamento header ogni 30 s (o immediato se l’NTP non è ancora valido)
  if (!g_timeSynced) { syncTimeOnce(); drawHeader(); lastHeaderUpdate = millis(); }
  else if (millis() - lastHeaderUpdate >= 30000) { drawHeader(); lastHeaderUpdate = millis(); }

  // Cambio immagine ogni IMAGE_DISPLAY_TIME se Wi-Fi e SD sono pronti
  if (WiFi.status() == WL_CONNECTED && sd_ok && sdCardPresent &&
      (millis() - lastImageChange >= IMAGE_DISPLAY_TIME)) {
    (void)downloadAndShowFromSD(); // ignora l’esito: errore visibile via Serial/log

    drawHeader();                  // ridisegna l’orario
    lastImageChange = millis();
  }

  delay(5);
}
