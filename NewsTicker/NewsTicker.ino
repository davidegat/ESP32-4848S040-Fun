/*
  Gat News Ticker – ESP32-S3 Panel-4848S040

  Funzioni:
  - Connessione Wi-Fi: prova STA con credenziali salvate; se assenti/apparse, avvia AP + captive portal.
  - RSS: aggrega 4 feed, parsing titoli/link, dedup, ordine random globale.
  - Render: 4 titoli per pagina, cambio ogni 30 s, aggiornamento feed ogni 10 min.
  - NTP: sincronizza ora una volta dopo connessione WiFi, mostra nel header (gg/mm hh:mm)
*/

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>
#include <time.h>

// =========================== Configurazione hardware display ===========================
#define GFX_BL      38
#define PWM_CHANNEL 0
#define PWM_FREQ    1000
#define PWM_BITS    8

// ----------------- Config NTP -----------------
static const char* NTP_SERVER = "pool.ntp.org";
// Fuso Italia/Svizzera: UTC+1 con DST +1
static const long   GMT_OFFSET_SEC      = 3600;
static const int    DAYLIGHT_OFFSET_SEC = 3600;

static bool g_timeSynced = false;

// Bus SPI software per comandi ST7701
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /*DC*/, 39 /*CS*/,
  48 /*SCK*/, 47 /*MOSI*/, GFX_NOT_DEFINED /*MISO*/
);

// Bus RGB parallelo per trasferimento pixel
Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  18, 17, 16, 21,      // DE, VSYNC, HSYNC, PCLK
  11, 12, 13, 14, 0,   // R0..R4
  8, 20, 3, 46, 9, 10, // G0..G5
  4, 5, 6, 7, 15,      // B0..B4
  1, 10, 8, 50,        // HSYNC: pol, front porch, pulse, back porch
  1, 10, 8, 20,        // VSYNC: pol, front porch, pulse, back porch
  0,                   // PCLK active neg
  12000000,            // clock pixel
  false, 0, 0, 0
);

// Display RGB con init ST7701 type9
Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480,
  rgbpanel, 0, true,
  bus, GFX_NOT_DEFINED,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// =========================== Wi-Fi / Captive portal ===========================
Preferences prefs;
DNSServer dnsServer;
WebServer  web(80);

String sta_ssid, sta_pass;
String ap_ssid, ap_pass;

const byte DNS_PORT = 53;

// =========================== Feed RSS ===========================
const char* FEEDS[4] = {
  "https://www.ansa.it/sito/ansait_rss.xml",
  "https://www.ilsole24ore.com/rss/mondo.xml",
  "https://www.ilsole24ore.com/rss/italia.xml",
  "https://www.fanpage.it/feed/"
};

// =========================== Dati notizie ===========================
static const int MAX_ITEMS = 120;
String titles[MAX_ITEMS];
String links [MAX_ITEMS];
int itemCount = 0;

static const uint32_t PAGE_DURATION_MS   = 30000;   // cambio pagina
static const uint32_t REFRESH_INTERVAL_MS= 600000;  // aggiornamento feed

uint32_t lastPageSwitch = 0;
uint32_t lastRefresh    = 0;
int currentPage = 0;
static const int ITEMS_PER_PAGE = 4;

// =========================== Grafica ===========================
#define RGB565_ORANGE 0xFD20
#define RGB565_BLACK  0x0000
#define RGB565_WHITE  0xFFFF

static const int HEADER_H = 56;
static const int PAGE_X   = 16;
static const int PAGE_Y   = HEADER_H + 12;
static const int PAGE_W   = 480 - 32;
static const int PAGE_H   = 480 - PAGE_Y - 16;

static const int ITEM_BOX_H      = PAGE_H / ITEMS_PER_PAGE;
static const int ITEM_MARGIN_X   = 10;
static const int ITEM_MARGIN_TOP = 6;
static const int ITEM_LINE_SP    = 4;

static const int BASE_CHAR_W = 6;
static const int BASE_CHAR_H = 8;
static const int TEXT_SCALE  = 2;
static const int CHAR_W      = BASE_CHAR_W * TEXT_SCALE;
static const int CHAR_H      = BASE_CHAR_H * TEXT_SCALE;

// =========================== Retroilluminazione / Pannello ===========================
static void backlightOn() {
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 255);
}

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

// =========================== Sincronizzazione NTP ===========================
static bool waitForValidTime(uint32_t timeoutMs = 8000) {
  uint32_t t0 = millis();
  time_t now = 0;
  struct tm info;
  
  while ((millis() - t0) < timeoutMs) {
    time(&now);
    localtime_r(&now, &info);
    // Verifica che l'anno sia ragionevole (> 2020)
    if (info.tm_year + 1900 > 2020) {
      return true;
    }
    delay(100);
  }
  return false;
}

static void syncTimeFromNTP() {
  if (g_timeSynced) return;
  
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  if (waitForValidTime(8000)) {
    g_timeSynced = true;
    Serial.println("NTP sync OK");
  } else {
    Serial.println("NTP sync failed");
  }
}

static String getFormattedDateTime() {
  if (!g_timeSynced) return "";
  
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%02d/%02d %02d:%02d", 
           timeinfo.tm_mday, 
           timeinfo.tm_mon + 1, 
           timeinfo.tm_hour, 
           timeinfo.tm_min);
  return String(buffer);
}

// =========================== Normalizzazione testo ===========================
// Tutte le virgolette/apici sono convertiti in apostrofo semplice ('), accenti ridotti a base.
static String normalizeAndTransliterate(const String& in) {
  String s = in;

  s.replace("\xC2\xA0", " ");   // NBSP

  s.replace("à","a"); s.replace("À","A");
  s.replace("è","e"); s.replace("È","E");
  s.replace("é","e"); s.replace("É","E");
  s.replace("ì","i"); s.replace("Ì","I");
  s.replace("ò","o"); s.replace("Ò","O");
  s.replace("ù","u"); s.replace("Ù","U");

  // Virgolette/apici/backtick/prime -> '
  s.replace("\"","'");
  s.replace("`","'");
  s.replace("\xC2\xB4","'");
  s.replace("\xE2\x80\x98","'");
  s.replace("\xE2\x80\x99","'");
  s.replace("\xE2\x80\xBA","'");
  s.replace("\xE2\x80\xB9","'");
  s.replace("\xCA\xB9","'");
  s.replace("\xE2\x80\xB2","'");
  s.replace("\xCA\xBC","'");
  s.replace("\xE2\x80\x9C","'");
  s.replace("\xE2\x80\x9D","'");
  s.replace("\xE2\x80\x9E","'");
  s.replace("\xE2\x80\x9F","'");
  s.replace("\xCA\xBA","'");
  s.replace("\xE2\x80\xB3","'");
  s.replace("\xC2\xAB","'");
  s.replace("\xC2\xBB","'");

  s.trim();
  while (s.indexOf("  ") >= 0) s.replace("  ", " ");
  return s;
}

// =========================== Disegno testo ===========================
static void drawBoldText(int16_t x, int16_t y, const String& raw) {
  String s = normalizeAndTransliterate(raw);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(RGB565_ORANGE, RGB565_BLACK);
  gfx->setCursor(x+1, y);   gfx->print(s);
  gfx->setCursor(x,   y+1); gfx->print(s);
  gfx->setCursor(x+1, y+1); gfx->print(s);
  gfx->setCursor(x,   y);   gfx->print(s);
}

static void drawBoldTextColored(int16_t x, int16_t y, const String& raw, uint16_t fg, uint16_t bg) {
  String s = normalizeAndTransliterate(raw);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x+1, y);   gfx->print(s);
  gfx->setCursor(x,   y+1); gfx->print(s);
  gfx->setCursor(x+1, y+1); gfx->print(s);
  gfx->setCursor(x,   y);   gfx->print(s);
}

static int approxTextWidthPx(const String& s) { return s.length() * CHAR_W; }

// =========================== Pulizia/decodifica XML ===========================
static String stripTagsAndDecode(String s) {
  int c0 = s.indexOf("<![CDATA[");
  if (c0 >= 0) { int c1 = s.indexOf("]]>", c0); if (c1 > c0) s = s.substring(c0 + 9, c1); }

  String out; out.reserve(s.length());
  bool inTag = false;
  for (size_t i=0;i<s.length();++i) {
    char ch = s[i];
    if (ch == '<') { inTag = true; continue; }
    if (ch == '>') { inTag = false; continue; }
    if (!inTag) out += ch;
  }

  out.replace("&amp;","&"); out.replace("&lt;","<"); out.replace("&gt;",">");
  out.replace("&quot;","'"); out.replace("&apos;","'"); out.replace("&nbsp;"," ");
  out.trim();
  while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  return normalizeAndTransliterate(out);
}

// =========================== Wrapping testo in box ===========================
static void drawWrappedInBox(int bx, int by, int bw, int bh, const String& text) {
  const int lineHeight = CHAR_H + ITEM_LINE_SP;
  const int maxLines   = (bh - ITEM_MARGIN_TOP*2) / lineHeight;
  const int maxWidth   = bw - ITEM_MARGIN_X*2;

  int y = by + ITEM_MARGIN_TOP + CHAR_H;
  String s = text;
  int start = 0;

  for (int line=0; line<maxLines && start < s.length(); ++line) {
    int end = start, lastSpace = -1;
    while (end < (int)s.length()) {
      String candidate = s.substring(start, end+1);
      if (candidate.endsWith(" ")) lastSpace = end;
      if (approxTextWidthPx(candidate) > maxWidth) break;
      end++;
    }
    if (end == (int)s.length()) {
      String chunk = s.substring(start, end); chunk.trim();
      drawBoldText(bx + ITEM_MARGIN_X, y, chunk);
      start = end; break;
    } else {
      int cut = (lastSpace >= start) ? lastSpace : (end - 1);
      String chunk = s.substring(start, cut + 1); chunk.trim();
      drawBoldText(bx + ITEM_MARGIN_X, y, chunk);
      start = cut + 1;
      while (start < (int)s.length() && s[start] == ' ') start++;
    }
    y += lineHeight;
  }
}

// =========================== Captive portal: pagine/handler ===========================
static String htmlIndex() {
  String ip = WiFi.softAPIP().toString();
  String page =
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>WiFi Setup</title></head><body style='font-family:sans-serif'>"
    "<h2>Configura WiFi</h2>"
    "<form method='POST' action='/save'>"
    "<label>SSID</label><br/><input name='ssid' style='width:280px'/><br/><br/>"
    "<label>Password</label><br/><input name='pass' type='password' style='width:280px'/><br/><br/>"
    "<button type='submit'>Salva & Connetti</button>"
    "</form>"
    "<p>Se il popup non compare, apri <b>http://" + ip + "/</b></p>"
    "</body></html>";
  return page;
}

static void handleRoot()  { web.send(200, "text/html", htmlIndex()); }

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

static void handleReboot(){ web.send(200, "text/plain", "OK"); delay(100); ESP.restart(); }

static void startDNSCaptive(){ dnsServer.start(DNS_PORT, "*", WiFi.softAPIP()); }

static void startWebPortal(){
  web.on("/", HTTP_GET, handleRoot);
  web.on("/save", HTTP_POST, handleSave);
  web.on("/reboot", HTTP_GET, handleReboot);
  web.onNotFound(handleRoot);
  web.begin();
}

// =========================== UI: header e schermata AP ===========================
static void drawHeader() {
  gfx->fillRect(0, 0, 480, HEADER_H, RGB565_ORANGE);
  
  // Disegna il titolo a sinistra
  drawBoldTextColored(16, 28, "Gat News Ticker", RGB565_BLACK, RGB565_ORANGE);
  
  // Disegna data/ora a destra se disponibile
  String datetime = getFormattedDateTime();
  if (datetime.length() > 0) {
    // Calcola posizione per allineare a destra
    int textWidth = datetime.length() * CHAR_W;
    int xPos = 480 - textWidth - 16;
    drawBoldTextColored(xPos, 28, datetime, RGB565_BLACK, RGB565_ORANGE);
  }
}

static void drawAPScreenOnce(const String& ssid, const String& pass)
{
  gfx->fillScreen(RGB565_BLACK);
  drawBoldText(16, 36, "Connettiti all'AP:");
  drawBoldText(16, 66, ssid);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(16, 96);  gfx->print("Password: "); gfx->print(pass);
  gfx->setCursor(16, 114); gfx->print("Captive portal automatico.");
  gfx->setCursor(16, 126); gfx->print("Se non compare, apri l'IP dell'AP.");
  gfx->setTextSize(TEXT_SCALE);
}

// =========================== Wi-Fi: AP bootstrap / STA connect ===========================
static void startAPWithPortal()
{
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssidbuf[32]; snprintf(ssidbuf, sizeof(ssidbuf), "PANEL-%02X%02X", mac[4], mac[5]);
  ap_ssid = ssidbuf;
  ap_pass = "panelsetup";

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
  delay(100);

  startDNSCaptive();
  startWebPortal();
  drawAPScreenOnce(ap_ssid, ap_pass);
}

static bool tryConnectSTA(uint32_t timeoutMs = 8000)
{
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
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(100);
    yield();
  }
  return WiFi.status() == WL_CONNECTED;
}

// =========================== Parsing RSS / utilities ===========================
static inline bool isHttpOk(int code) { return (code >= 200 && code < 300); }

static int indexOfCI(const String& s, const String& pat, int from=0) {
  String S = s; S.toLowerCase();
  String P = pat; P.toLowerCase();
  return S.indexOf(P, from);
}

static String extractTag(const String& xml, const String& tag, int& fromIdx) {
  String open = "<" + tag + ">";
  String close = "</" + tag + ">";
  int s = indexOfCI(xml, open, fromIdx);
  if (s < 0) return "";
  s += open.length();
  int e = indexOfCI(xml, close, s);
  if (e < 0) return "";
  fromIdx = e + close.length();
  String raw = xml.substring(s, e);
  raw.trim();
  return raw;
}

// Estrae <item> e popola titoli/link; dedup su titolo o link identico.
static void parseFeedItems(const String& body) {
  int pos = 0;
  extractTag(body, "title", pos); // salta titolo canale

  int start;
  while ((start = indexOfCI(body, "<item", pos)) >= 0) {
    int openEnd = body.indexOf('>', start);
    if (openEnd < 0) break;
    int end = indexOfCI(body, "</item>", openEnd);
    if (end < 0) break;

    String item = body.substring(openEnd + 1, end);
    int ipos = 0;
    String t = extractTag(item, "title", ipos);
    ipos = 0;
    String l = extractTag(item, "link", ipos);

    t = stripTagsAndDecode(t);
    l.trim();

    if (t.length() && l.length()) {
      bool dup = false;
      for (int i=0;i<itemCount;i++) {
        if (titles[i] == t || links[i] == l) { dup = true; break; }
      }
      if (!dup && itemCount < MAX_ITEMS) {
        titles[itemCount] = t;
        links[itemCount]  = l;
        itemCount++;
      }
    }
    pos = end + 7; // "</item>"
  }

  // Fallback per feed senza blocchi <item>
  if (itemCount == 0) {
    int p = 0; extractTag(body, "title", p);
    for (int i=0;i<20 && itemCount < MAX_ITEMS; ++i) {
      String t = extractTag(body, "title", p);
      String l = extractTag(body, "link",  p);
      if (t.isEmpty() || l.isEmpty()) break;
      t = stripTagsAndDecode(t); l.trim();
      bool dup = false;
      for (int k=0;k<itemCount;k++) if (titles[k]==t || links[k]==l) { dup=true; break; }
      if (!dup) { titles[itemCount]=t; links[itemCount]=l; itemCount++; }
    }
  }
}

// Shuffle Fisher–Yates su coppie titolo/link
static void shuffleItems() {
  if (itemCount <= 1) return;
  for (int i = itemCount - 1; i > 0; --i) {
    int j = random(i + 1);
    if (i != j) {
      String t = titles[i]; titles[i] = titles[j]; titles[j] = t;
      String l = links [i]; links [i] = links [j]; links [j] = l;
    }
  }
}

// Scarica tutti i feed, ricompone la lista e rimescola
static void refreshAllFeeds() {
  itemCount = 0;
  for (int f=0; f<4; ++f) {
    const char* url = FEEDS[f];
    if (!url || !url[0]) continue;

    HTTPClient http; http.setTimeout(8000);
    if (!http.begin(url)) continue;
    int code = http.GET();
    if (!isHttpOk(code)) { http.end(); continue; }
    String body = http.getString();
    http.end();

    parseFeedItems(body);
  }
  shuffleItems();
  currentPage = 0;
  lastPageSwitch = millis();
}

// =========================== Render pagina ===========================
static void drawNewsPage(int pageIdx) {
  gfx->fillScreen(RGB565_BLACK);
  drawHeader();

  for (int i=0; i<ITEMS_PER_PAGE; ++i) {
    int itemIdx = pageIdx * ITEMS_PER_PAGE + i;
    if (itemCount == 0 || itemIdx >= itemCount) break;

    int by = PAGE_Y + i * ITEM_BOX_H;
    if (i > 0) gfx->drawLine(PAGE_X, by, PAGE_X + PAGE_W, by, RGB565_ORANGE);

    drawWrappedInBox(PAGE_X, by, PAGE_W, ITEM_BOX_H, titles[itemIdx]);
  }

  int totalPages = (itemCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
  if (totalPages == 0) totalPages = 1;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d/%d", (pageIdx % totalPages) + 1, totalPages);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(440, 470); gfx->print(buf);
  gfx->setTextSize(TEXT_SCALE);
}

// =========================== Setup / Loop ===========================
void setup() {
  Serial.begin(115200);
  
  backlightOn();
  panelKickstart();

  randomSeed(esp_timer_get_time());

  gfx->fillScreen(RGB565_BLACK);
  drawBoldText(16, 36, "WiFi: connessione...");

  if (!tryConnectSTA(8000)) {
    startAPWithPortal();
  } else {
    // Sincronizza ora da NTP dopo connessione WiFi riuscita
    syncTimeFromNTP();
    
    refreshAllFeeds();
    lastRefresh = millis();
    drawNewsPage(currentPage);
  }
}

void loop() {
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest();
    web.handleClient();
    delay(10);
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t nextTry = 0;
    if (millis() > nextTry) { 
      nextTry = millis() + 5000; 
      if (tryConnectSTA(5000)) {
        // Risincronizza NTP dopo riconnessione
        if (!g_timeSynced) {
          syncTimeFromNTP();
        }
      }
    }
    delay(40);
    return;
  }

  if (millis() - lastRefresh >= REFRESH_INTERVAL_MS) {
    lastRefresh = millis();
    refreshAllFeeds();
    drawNewsPage(currentPage);
  }

  if (millis() - lastPageSwitch >= PAGE_DURATION_MS) {
    lastPageSwitch = millis();
    int totalPages = (itemCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    if (totalPages == 0) totalPages = 1;
    currentPage = (currentPage + 1) % totalPages;
    drawNewsPage(currentPage);
  }

  delay(5);
}