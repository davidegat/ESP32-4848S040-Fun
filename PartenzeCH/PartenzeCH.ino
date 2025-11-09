/*
  Gat Panel – Departures (ESP32-S3 Panel-4848S040) + Touch Presets Cycle

  Scopo
  - Pannello 480×480 via Arduino_GFX + pannello ST7701 (type9)
  - Orologio NTP e intestazioni
  - Connessioni via transport.opendata.ch (connections)
  - Filtro: scarta partenze entro 60 s dall'istante
  - Durata >60 min resa come XhYm; altrimenti Xm
  - Ritardi: riga testo rossa se delay>0
  - Web UI /route: from/to, refresh (60–3600), preset (NVS JSON)
  - Captive portal Wi‑Fi se mancano credenziali
  - Touch GT911: tap singolo cicla preset temporanei (senza scrivere in NVS)
*/

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino_GFX_Library.h>
#include <time.h>
#include <ArduinoJson.h>
#include <TAMC_GT911.h>

// =========================== Display / PWM backlight ===========================
#define GFX_BL      38
#define PWM_CHANNEL 0
#define PWM_FREQ    1000
#define PWM_BITS    8

// =========================== NTP ===============================
static const char* NTP_SERVER = "pool.ntp.org";
static const long   GMT_OFFSET_SEC      = 3600;   // UTC+1
static const int    DAYLIGHT_OFFSET_SEC = 3600;   // DST
static bool g_timeSynced = false;

// =========================== Palette / metrica testo ==========================
#define RGB565_BLACK   0x0000
#define RGB565_WHITE   0xFFFF
#define RGB565_DKGREY  0x18E3
#define RGB565_BLUE    0x081F   // blu profondo tipo FFS (display)
#define RGB565_RED     0xE880   // rosso FFS più caldo

static const int HEADER_H    = 56;
static const int BASE_CHAR_W = 6;
static const int BASE_CHAR_H = 8;
static const int TEXT_SCALE  = 2;
static const int CHAR_W      = BASE_CHAR_W * TEXT_SCALE;
static const int CHAR_H      = BASE_CHAR_H * TEXT_SCALE;
static const int ROUTE_H     = 20;
static const int SUBHDR_H    = 28;
static const int CONTENT_Y   = HEADER_H + ROUTE_H + SUBHDR_H;
static const int ROW_H       = 34;
static const int COL1_X      = 14;    // ORA
static const int COL2_X      = 128;   // LINEA
static const int COL3_X      = 270;   // DURATA
static const int COL4_X      = 400;   // CAMBI

// =========================== Touch GT911 (pin & mapping) ======================
#define I2C_SDA_PIN   19
#define I2C_SCL_PIN   45
#define TOUCH_INT     -1   // usare -1 se il pannello non espone INT
#define TOUCH_RST     -1   // usare -1 se il pannello non espone RST

// Mappa coordinate (origine in alto a sinistra, 480×480)
#define TOUCH_MAP_X1  480
#define TOUCH_MAP_X2  0
#define TOUCH_MAP_Y1  480
#define TOUCH_MAP_Y2  0

// =========================== Dati riga tabella ================================
struct Row {
  String timeHHMM;     // orario HH:MM
  String line;         // etichetta linea (es. IC 21 / RE 8 / numero)
  String duration;     // stringa durata formattata
  String transfers;    // numero cambi -> stringa
  int    delayMin = 0; // ritardo stimato in minuti
};

// =========================== Bus / pannello RGB ===============================
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /*DC*/, 39 /*CS*/,
  48 /*SCK*/, 47 /*MOSI*/, GFX_NOT_DEFINED /*MISO*/
);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  /* HSYNC, VSYNC, DE, PCLK */ 18, 17, 16, 21,
  /* R0..R4 */ 11, 12, 13, 14, 0,
  /* G0..G5 */ 8, 20, 3, 46, 9, 10,
  /* B0..B4 */ 4, 5, 6, 7, 15,
  /* hfp, hbp, hsync, vfp */ 1, 10, 8, 50,
  /* vbp, vsync, pclk_active_neg, prefer_speed */ 1, 10, 8, 20,
  /* auto_flush */ 0,
  /* pclk (Hz)  */ 12000000,
  /* useBigEndian */ false, 0, 0, 0
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480, rgbpanel, 0, true, bus, GFX_NOT_DEFINED,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// =========================== Touch controller ================================
TAMC_GT911 ts(
  I2C_SDA_PIN, I2C_SCL_PIN, TOUCH_INT, TOUCH_RST,
  (TOUCH_MAP_X1 > TOUCH_MAP_X2 ? TOUCH_MAP_X1 : TOUCH_MAP_X2),
  (TOUCH_MAP_Y1 > TOUCH_MAP_Y2 ? TOUCH_MAP_Y1 : TOUCH_MAP_Y2)
);

// Rilevazione tap robusta (edge + quiet + cooldown)
static const uint32_t TAP_MIN_MS      = 40;
static const uint32_t TAP_MAX_MS      = 400;
static const uint16_t TAP_MOVE_TOL_PX = 24;
static const uint32_t TAP_QUIET_MS    = 500;
static const uint32_t TAP_COOLDOWN_MS = 1000;
static const uint32_t TOUCH_REINIT_MS = 15000; // watchdog re-init driver

static bool     g_touchDown = false;
static uint32_t g_touchDownMs = 0;
static int16_t  g_touchStartX = -1, g_touchStartY = -1;
static bool     g_touchMovedTooFar = false;
static uint32_t g_lastReleaseMs = 0;
static uint32_t g_lastTapMs = 0;
static uint32_t g_touchLastActivityMs = 0;
static int      g_touchPresetIdx = -1; // -1 = route base

// =========================== Networking / stato ===============================
DNSServer dnsServer;
WebServer  web(80);
Preferences prefs;         // credenziali Wi‑Fi
String sta_ssid, sta_pass, ap_ssid, ap_pass;
const byte DNS_PORT = 53;

// =========================== Route / API config ===============================
Preferences routePrefs;     // namespace "route"
String g_from = "Bellinzona";
String g_to   = "Lugano";
String g_apiUrl;            // URL costruita da buildApiUrl()
static const int ROWS_MAX = 11;
static volatile bool g_routeChanged = false;
static uint32_t g_refreshSec = 300; // [60..3600]
static uint32_t g_lastFetchMs = 0;

// =========================== Preset (NVS) =====================
struct Preset { String label, from, to; };
static const int MAX_PRESETS = 12;
Preset g_presets[MAX_PRESETS];
int g_presetCount = 0;

// =========================== Prototipi ========================
static bool   fetchDepartures(Row* outRows, int &count);
static void   initTouch(bool showOverlay=true);
static bool   touchTapDetected();
static void   applyPresetByIndex(int idx);

// =========================== Helpers disegno ==================
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
  delay(60);
}

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

static void syncTimeOnce() {
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  g_timeSynced = waitForValidTime();
}

static String fmtDateTime() {
  if (!g_timeSynced) return "";
  time_t now; struct tm ti; time(&now); localtime_r(&now, &ti);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d/%02d - %02d:%02d",
           ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
  return String(buf);
}

static String normalize(const String& in) {
  String s = in;
  s.replace("Â "," ");
  s.replace("à","a"); s.replace("è","e"); s.replace("é","e");
  s.replace("ì","i"); s.replace("ò","o"); s.replace("ù","u");
  s.replace("\"","'");
  return s;
}

static void drawBoldTextColored(int16_t x, int16_t y, const String& raw, uint16_t fg, uint16_t bg) {
  String s = normalize(raw);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x+1, y);   gfx->print(s);
  gfx->setCursor(x,   y+1); gfx->print(s);
  gfx->setCursor(x+1, y+1); gfx->print(s);
  gfx->setCursor(x,   y);   gfx->print(s);
}

static void drawHeader() {
  gfx->fillRect(0, 0, 480, HEADER_H, RGB565_RED);
  drawBoldTextColored(16, 16, "Partenze di oggi", RGB565_WHITE, RGB565_RED);
  String dt = fmtDateTime();
  if (dt.length()) {
    int w = dt.length() * CHAR_W;
    int x = 480 - w - 16;
    drawBoldTextColored(x, 16, dt, RGB565_WHITE, RGB565_RED);
  }
}

static void drawRouteBar() {
  gfx->fillRect(0, HEADER_H, 480, ROUTE_H, RGB565_RED);
  gfx->setTextSize(2);
  gfx->setTextColor(RGB565_WHITE, RGB565_RED);
  int y = HEADER_H + (ROUTE_H - 25) / 2;
  gfx->setCursor(16, y);
  gfx->print(normalize(g_from) + " -> " + normalize(g_to));
}

static void drawSubHeader() {
  gfx->fillRect(0, HEADER_H + ROUTE_H, 480, SUBHDR_H, RGB565_BLACK);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  int y = HEADER_H + ROUTE_H + (SUBHDR_H - CHAR_H) / 2;
  gfx->setCursor(COL1_X, y); gfx->print("ORA");
  gfx->setCursor(COL2_X, y); gfx->print("LINEA");
  gfx->setCursor(COL3_X, y); gfx->print("DURATA");
  gfx->setCursor(COL4_X, y); gfx->print("CAMBI");
  gfx->drawLine(10, HEADER_H + ROUTE_H + SUBHDR_H - 2, 470, HEADER_H + ROUTE_H + SUBHDR_H - 2, RGB565_DKGREY);
}

static void clearContentArea() { gfx->fillRect(0, CONTENT_Y, 480, 480 - CONTENT_Y, RGB565_BLUE); }

static void drawRow(int row, const String& t, const String& line, const String& dur, const String& tr) {
  int y = CONTENT_Y + 8 + row * ROW_H;
  if (row > 0) gfx->drawLine(10, y - 6, 470, y - 6, RGB565_WHITE);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setCursor(COL1_X, y); gfx->print(t);
  gfx->setCursor(COL2_X, y); gfx->print(line);
  gfx->setCursor(COL3_X, y); gfx->print(dur);
  gfx->setCursor(COL4_X, y); gfx->print(tr);
}

static void drawStatus(const char* msg) {
  clearContentArea();
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(16, CONTENT_Y + 10);
  gfx->print(msg);
}

static void forceRedrawNow() {
  drawHeader();
  drawRouteBar();
  drawSubHeader();
  // Fetch immediato dopo header/route
  // (mantiene identica la sequenza visiva originale)
  //
  // Nota: la logica di fetch rimane invariata.
  extern void doFetchAndRedraw();
  doFetchAndRedraw();
}

// =========================== Wi‑Fi base (STA) ==================
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
  return false;
}

// =========================== Captive portal ====================
static void startAP() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssidbuf[32]; snprintf(ssidbuf, sizeof(ssidbuf), "PANEL-%02X%02X", mac[4], mac[5]);
  ap_ssid = ssidbuf; ap_pass = "panelsetup";
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
}

static String ipToString(IPAddress ip) { return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3]; }

static String htmlWifiIndex() {
  String ip = ipToString(WiFi.softAPIP());
  String page =
    "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Wi-Fi Setup</title><style>"
    "body{font-family:sans-serif;background:#111;color:#eee;padding:24px}"
    "input,button{font-size:18px;padding:10px;margin:6px;width:100%;box-sizing:border-box}"
    "form{max-width:420px;margin:0 auto}"
    "</style></head><body>"
    "<h2>Configura Wi-Fi</h2>"
    "<form method='POST' action='/save'>"
    "<label>SSID</label><input name='ssid' required>"
    "<label>Password</label><input type='password' name='pass'>"
    "<button>Salva</button></form>"
    "<p>Se il popup non compare, apri <b>http://" + ip + "/</b></p>"
    "</body></html>";
  return page;
}

static void handleWifiRoot() { web.send(200, "text/html", htmlWifiIndex()); }

static void handleWifiSave() {
  if (web.hasArg("ssid") && web.hasArg("pass")) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    prefs.end();
    web.send(200, "text/html",
      "<html><body><h3>Salvate. Riavvio…</h3>"
      "<script>setTimeout(()=>{fetch('/reboot')},600);</script>"
      "</body></html>");
  } else {
    web.send(400, "text/plain", "Bad Request");
  }
}

static void handleWifiReboot(){ web.send(200, "text/plain", "OK"); delay(100); ESP.restart(); }
static void startDNSCaptive(){ dnsServer.start(DNS_PORT, "*", WiFi.softAPIP()); }

static void drawAPScreenOnce(const String& ssid, const String& pass) {
  gfx->fillScreen(RGB565_BLACK);
  drawBoldTextColored(16, 36, "Configura rete Wi-Fi", RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(16, 74);  gfx->print("Rete: "); gfx->print(ssid);
  gfx->setCursor(16, 92);  gfx->print("Password: "); gfx->print(pass);
  gfx->setCursor(16, 110); gfx->print("Se non compare, apri http://"); gfx->print(ipToString(WiFi.softAPIP()));
  gfx->setTextSize(TEXT_SCALE);
}

static void registerWebHandlers(bool portalMode) {
  web.on("/route", HTTP_GET, [](){
    IPAddress ip = (WiFi.getMode()==WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
    String ipStr = String(ip[0])+"."+ip[1]+"."+ip[2]+"."+ip[3];
    extern String buildRoutePage(const String&, const String&);
    web.send(200, "text/html", buildRoutePage(ipStr, ""));
  });

  web.on("/route", HTTP_POST, [](){
    String action = web.hasArg("action") ? web.arg("action") : "save_route";
    String notice;

    if (action == "save_route") {
      String nf = web.hasArg("from") ? web.arg("from") : "";
      String nt = web.hasArg("to")   ? web.arg("to")   : "";
      nf.trim(); nt.trim();
      if (!nf.length()) nf = "Bellinzona";
      if (!nt.length()) nt = "Lugano";

      if (web.hasArg("refresh")) {
        long v = web.arg("refresh").toInt();
        if (v < 60) v = 60; if (v > 3600) v = 3600;
        g_refreshSec = (uint32_t)v;
        routePrefs.begin("route", false);
        routePrefs.putUInt("refresh", g_refreshSec);
        routePrefs.end();
      }
      extern void saveRouteToNVS(const String&, const String&);
      saveRouteToNVS(nf, nt);
      g_from = nf; g_to = nt;
      extern void buildApiUrl();
      buildApiUrl();
      g_routeChanged = true;
      g_touchPresetIdx = -1; // torna alla route base

      notice = "Impostazioni salvate – aggiornamento schermo in corso. Percorso: <b>" + nf + " → " + nt + "</b> | Refresh: <b>" + String(g_refreshSec) + " s</b>";
    }
    else if (action == "save_preset") {
      int idx = web.hasArg("id") ? web.arg("id").toInt() : -1;
      String label = web.hasArg("label") ? web.arg("label") : "";
      String pf = web.hasArg("pfrom") ? web.arg("pfrom") : "";
      String pt = web.hasArg("pto")   ? web.arg("pto")   : "";
      label.trim(); pf.trim(); pt.trim();
      extern bool addOrUpdatePreset(int, const String&, const String&, const String&);
      extern void savePresetsToNVS();
      if (addOrUpdatePreset(idx, label, pf, pt)) { savePresetsToNVS(); notice = "Preset salvato: <b>"+label+"</b>"; }
      else { notice = "Errore: preset non salvato (campi vuoti o limite raggiunto)."; }
    }
    else if (action == "delete_preset") {
      int idx = web.hasArg("id") ? web.arg("id").toInt() : -1;
      extern bool deletePreset(int);
      extern void savePresetsToNVS();
      if (deletePreset(idx)) { savePresetsToNVS(); notice = "Preset eliminato."; }
      else { notice = "Errore: impossibile eliminare il preset."; }
    }
    else if (action == "apply_preset") {
      int idx = web.hasArg("id") ? web.arg("id").toInt() : -1;
      if (idx>=0 && idx<g_presetCount) {
        g_from = g_presets[idx].from;
        g_to   = g_presets[idx].to;
        extern void saveRouteToNVS(const String&, const String&);
        extern void buildApiUrl();
        saveRouteToNVS(g_from, g_to);
        buildApiUrl();
        g_routeChanged = true;
        g_touchPresetIdx = -1; // applicazione via Web = base
        notice = "Preset applicato: <b>"+g_presets[idx].label+"</b>. Aggiornamento schermo in corso.";
      } else {
        notice = "Errore: preset non valido.";
      }
    }

    IPAddress ip = (WiFi.getMode()==WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
    String ipStr = String(ip[0])+"."+ip[1]+"."+ip[2]+"."+ip[3];
    extern String buildRoutePage(const String&, const String&);
    web.send(200, "text/html", buildRoutePage(ipStr, notice));
  });

  if (portalMode) {
    web.on("/", HTTP_GET, handleWifiRoot);
    web.on("/save", HTTP_POST, handleWifiSave);
    web.on("/reboot", HTTP_GET, handleWifiReboot);
    web.onNotFound(handleWifiRoot);
  } else {
    web.on("/", HTTP_GET, [](){ web.sendHeader("Location", "/route", true); web.send(302, "text/plain", ""); });
    web.onNotFound([](){ web.sendHeader("Location", "/route", true); web.send(302, "text/plain", ""); });
  }
  web.begin();
}

static void startAPWithPortal() {
  startAP();
  startDNSCaptive();
  registerWebHandlers(true);
  drawAPScreenOnce(ap_ssid, ap_pass);
}

// =========================== Preset (lettura/scrittura) =======================
static void loadPresetsFromNVS() {
  Preferences pv; pv.begin("presets", true);
  String raw = pv.getString("list", "");
  pv.end();

  g_presetCount = 0;
  if (!raw.length()) return;

  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, raw)) return;

  JsonArray items = doc["items"].as<JsonArray>();
  if (items.isNull()) return;

  for (JsonObject it : items) {
    if (g_presetCount >= MAX_PRESETS) break;
    Preset p;
    p.label = (const char*)(it["label"] | "");
    p.from  = (const char*)(it["from"]  | "");
    p.to    = (const char*)(it["to"]    | "");
    p.label.trim(); p.from.trim(); p.to.trim();
    if (!p.label.length() || !p.from.length() || !p.to.length()) continue;
    g_presets[g_presetCount++] = p;
  }
  for (int i=0;i<g_presetCount;i++) {
    if (!g_presets[i].from.length() || !g_presets[i].to.length()) {
      for (int j=i+1;j<g_presetCount;j++) g_presets[j-1]=g_presets[j];
      --g_presetCount; --i;
    }
  }
}

static void savePresetsToNVS() {
  DynamicJsonDocument doc(4096);
  JsonArray items = doc.createNestedArray("items");
  for (int i=0;i<g_presetCount;i++) {
    JsonObject o = items.createNestedObject();
    o["label"] = g_presets[i].label;
    o["from"]  = g_presets[i].from;
    o["to"]    = g_presets[i].to;
  }
  String out; serializeJson(doc, out);
  Preferences pv; pv.begin("presets", false);
  pv.putString("list", out); pv.end();
}

static bool addOrUpdatePreset(int idx, const String& label, const String& from, const String& to) {
  if (label.length()==0 || from.length()==0 || to.length()==0) return false;
  for (int i=0;i<g_presetCount;i++) {
    if (i==idx) continue;
    if (g_presets[i].label == label) { if (idx<0) { g_presets[i].from = from; g_presets[i].to = to; return true; } }
  }
  if (idx>=0 && idx<g_presetCount) { g_presets[idx].label = label; g_presets[idx].from = from; g_presets[idx].to = to; return true; }
  if (g_presetCount >= MAX_PRESETS) return false;
  g_presets[g_presetCount++] = Preset{label, from, to};
  return true;
}

static bool deletePreset(int idx) {
  if (idx<0 || idx>=g_presetCount) return false;
  for (int i=idx+1; i<g_presetCount; i++) g_presets[i-1] = g_presets[i];
  g_presetCount--; return true;
}

// =========================== URL / route ======================
static String urlEncodeSpaces(const String &s) {
  String out; out.reserve(s.length()*3);
  for (size_t i=0; i<s.length(); ++i) { char c = s[i]; out += (c==' ')? "%20" : String(c); }
  return out;
}

static void buildApiUrl() {
  String fromEnc = urlEncodeSpaces(g_from);
  String toEnc   = urlEncodeSpaces(g_to);
  g_apiUrl  = "https://transport.opendata.ch/v1/connections";
  g_apiUrl += "?from=" + fromEnc + "&to=" + toEnc + "&limit=" + String(ROWS_MAX + 5);
  g_apiUrl += "&fields[]=connections/from/departure";
  g_apiUrl += "&fields[]=connections/from/departureTimestamp";
  g_apiUrl += "&fields[]=connections/from/prognosis/departure";
  g_apiUrl += "&fields[]=connections/from/prognosis/departureTimestamp";
  g_apiUrl += "&fields[]=connections/duration";
  g_apiUrl += "&fields[]=connections/transfers";
  g_apiUrl += "&fields[]=connections/products";
  g_apiUrl += "&fields[]=connections/sections/journey/name";
  g_apiUrl += "&fields[]=connections/sections/journey/category";
  g_apiUrl += "&fields[]=connections/sections/journey/number";
}

static void loadRouteFromNVS() {
  routePrefs.begin("route", true);
  String f = routePrefs.getString("from", "");
  String t = routePrefs.getString("to",   "");
  uint32_t rs = routePrefs.getUInt("refresh", 300);
  routePrefs.end();
  if (f.length()) g_from = f;
  if (t.length()) g_to   = t;
  if (rs < 60 || rs > 3600) rs = 300;
  g_refreshSec = rs;
  buildApiUrl();
}

static void saveRouteToNVS(const String &from, const String &to) {
  routePrefs.begin("route", false);
  routePrefs.putString("from", from);
  routePrefs.putString("to",   to);
  routePrefs.end();
}

// =========================== Helpers orario/linea =============
static String trimSpaces(String s) {
  while (s.length() && s[0]==' ') s.remove(0,1);
  while (s.length() && s[s.length()-1]==' ') s.remove(s.length()-1);
  return s;
}

static String hhmmFromISO(const char* iso) {
  if (!iso) return "";
  const char* t = strchr(iso, 'T');
  if (!t || strlen(t) < 6) return "";
  char hhmm[6] = { t[1], t[2], ':', t[4], t[5], 0 };
  return String(hhmm);
}

static String hhmmFromTimestamp(long ts) {
  if (ts <= 0) return "";
  time_t tt = (time_t)ts; struct tm ti; localtime_r(&tt, &ti);
  char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", ti.tm_hour, ti.tm_min);
  return String(buf);
}

static String extractHHMM(JsonObject conn) {
  long ts = (long)(conn["from"]["departureTimestamp"] | 0L);
  String hhmm = hhmmFromTimestamp(ts);
  if (!hhmm.length()) {
    const char* iso = conn["from"]["departure"] | nullptr;
    hhmm = hhmmFromISO(iso);
  }
  if (!hhmm.length()) hhmm = "--:--";
  return hhmm;
}

static String extractLineLabel(JsonObject conn) {
  if (conn.containsKey("products")) {
    JsonArray prods = conn["products"].as<JsonArray>();
    if (!prods.isNull() && prods.size() > 0) {
      String p0 = (const char*)(prods[0] | "");
      p0 = trimSpaces(p0);
      if (p0.length()) return p0;
    }
  }
  JsonArray sections = conn["sections"].as<JsonArray>();
  if (!sections.isNull()) {
    for (JsonObject sec : sections) {
      JsonObject j = sec["journey"].as<JsonObject>();
      if (!j.isNull()) {
        String cat = (const char*)(j["category"] | "");
        String num = (const char*)(j["number"]   | "");
        String nam = (const char*)(j["name"]     | "");
        String lab;
        if (cat.length()) lab += cat;
        if (num.length()) { if (lab.length()) lab += " "; lab += num; }
        if (!lab.length() && nam.length()) lab = nam;
        lab = trimSpaces(lab);
        if (lab.length()) return lab;
      }
    }
  }
  return "—";
}

// =========================== Filtro "entro 60s" ===============
static bool isoIsThisMinute(const char* iso) {
  if (!g_timeSynced || !iso) return false;
  const char* t = strchr(iso, 'T'); if (!t || strlen(t) < 6) return false;
  int hh = (t[1]-'0')*10 + (t[2]-'0');
  int mm = (t[4]-'0')*10 + (t[5]-'0');
  time_t now; struct tm ti; time(&now); localtime_r(&now, &ti);
  return (ti.tm_hour == hh && ti.tm_min == mm);
}

static bool shouldSkipSoon(long ts, const char* iso) {
  if (!g_timeSynced) return false;
  time_t now; time(&now);
  if (ts > 0 && ts <= (long)now + 60) return true;
  if ((ts <= 0) && isoIsThisMinute(iso)) return true;
  return false;
}

// =========================== Ritardo (minuti) ==================
static int parseDelayMinutes(JsonObject fromObj) {
  long sched = (long)(fromObj["departureTimestamp"] | 0L);
  long prog  = (long)(fromObj["prognosis"]["departureTimestamp"] | 0L);

  if (prog == 0) {
    const char* isoProg = fromObj["prognosis"]["departure"] | nullptr;
    if (isoProg) {
      int Y,M,D,h,m,s; if (sscanf(isoProg, "%d-%d-%dT%d:%d:%d", &Y,&M,&D,&h,&m,&s) == 6) {
        struct tm tmv{}; tmv.tm_year=Y-1900; tmv.tm_mon=M-1; tmv.tm_mday=D; tmv.tm_hour=h; tmv.tm_min=m; tmv.tm_sec=s;
        prog = mktime(&tmv);
      }
    }
  }
  if (sched>0 && prog>0 && prog>sched) { long d = prog - sched; return (int)((d + 59)/60); }
  return 0;
}

// =========================== Transport API (fetch) ============
static bool fetchDepartures(Row* outRows, int &count) {
  count = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client; client.setInsecure(); client.setTimeout(12000);
  HTTPClient http;
  if (!http.begin(client, g_apiUrl)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(32768);
  if (deserializeJson(doc, payload)) return false;

  JsonArray conns = doc["connections"].as<JsonArray>();
  if (conns.isNull()) return false;

  for (JsonObject c : conns) {
    if (count >= ROWS_MAX) break;

    long tsdep = (long)(c["from"]["departureTimestamp"] | 0L);
    const char* iso = c["from"]["departure"] | nullptr;
    if (shouldSkipSoon(tsdep, iso)) continue;

    String hhmm = hhmmFromTimestamp(tsdep);
    if (!hhmm.length()) hhmm = hhmmFromISO(iso);
    if (!hhmm.length()) hhmm = "--:--";

    String line = extractLineLabel(c);

    // Durata: XhYm (>60m) altrimenti Xm
    String durField = (const char*)(c["duration"] | "");
    int totalMin = 0; int h = 0, m = 0;
    if (durField.length() >= 8) {
      String tail = durField.substring(durField.length() - 8);
      h = tail.substring(0, 2).toInt();
      m = tail.substring(3, 5).toInt();
      totalMin = h * 60 + m;
    }
    String durStr;
    if (totalMin > 60) { char buf[16]; snprintf(buf, sizeof(buf), "%dh%dm", h, m); durStr = buf; }
    else if (totalMin > 0) { durStr = String(totalMin) + "m"; }
    else { durStr = "--"; }

    int transfers = (int)(c["transfers"] | 0);
    int delayMin  = parseDelayMinutes(c["from"].as<JsonObject>());

    Row r; r.timeHHMM = hhmm; r.line = line; r.duration = durStr; r.transfers = String(transfers); r.delayMin = delayMin;
    outRows[count++] = r;
  }
  return count > 0;
}

// =========================== Web UI (HTML helpers) ============
static String htmlEscape(const String& s) {
  String o; o.reserve(s.length()*2);
  for (size_t i=0;i<s.length();++i) {
    char c=s[i];
    if (c=='&') o += "&amp;";
    else if (c=='<') o += "&lt;";
    else if (c=='>') o += "&gt;";
    else if (c=='\"') o += "&quot;";
    else o += c;
  }
  return o;
}

static String buildPresetsHTML() {
  String html;
  html += "<h3>Preset</h3>";
  html += "<div class='box'><p class='hint'>Salva i percorsi come preset personali (max ";
  html += String(MAX_PRESETS);
  html += "). Tap sul display per ciclarli, senza modificare l’NVS.</p>";

  html +=
    "<form id='pform' method='POST' action='/route'>"
    "<input type='hidden' name='action' value='save_preset'>"
    "<input type='hidden' id='pid' name='id' value='-1'>"
    "<label>Nome preset</label>"
    "<input id='plabel' name='label' placeholder='Es. Casa → Lavoro'>"
    "<div class='grid2'>"
      "<div><label>Partenza</label><input id='pfrom' name='pfrom' placeholder='Es. Bellinzona'></div>"
      "<div><label>Arrivo</label><input id='pto'   name='pto'   placeholder='Es. Lugano'></div>"
    "</div>"
    "<div class='grid2'>"
      "<button type='submit'>Salva preset</button>"
      "<button type='button' onclick='fillFromCurrent()'>Compila dai campi sopra</button>"
    "</div>"
    "</form>";

  html += "<div class='list'>";
  if (g_presetCount==0) {
    html += "<p class='hint'>Nessun preset salvato.</p>";
  } else {
    for (int i=0;i<g_presetCount;i++) {
      String lbl = htmlEscape(g_presets[i].label);
      String fr  = htmlEscape(g_presets[i].from);
      String to  = htmlEscape(g_presets[i].to);
      html +=
        "<div class='item'>"
          "<div class='itxt'><b>" + lbl + "</b><br><span>" + fr + " → " + to + "</span></div>"
          "<div class='iact'>"
            "<form method='POST' action='/route' style='display:inline'>"
              "<input type='hidden' name='action' value='apply_preset'>"
              "<input type='hidden' name='id' value='"+String(i)+"'>"
              "<button>Usa</button></form>"
            "<button type='button' onclick='editPreset("+String(i)+")'>Modifica</button>"
            "<form method='POST' action='/route' style='display:inline' onsubmit='return confirm(\"Eliminare preset?\")'>"
              "<input type='hidden' name='action' value='delete_preset'>"
              "<input type='hidden' name='id' value='"+String(i)+"'>"
              "<button class='danger'>Elimina</button></form>"
          "</div>"
        "</div>";
    }
  }
  html += "</div></div>";

  html +=
    "<script>"
    "function fillFromCurrent(){"
      "document.getElementById('pfrom').value=document.getElementById('from').value;"
      "document.getElementById('pto').value=document.getElementById('to').value;"
    "}"
    "function editPreset(i){"
      "var L="+String(g_presetCount)+";"
      "var labels=[";
  for (int i=0;i<g_presetCount;i++) { if (i) html += ","; html += "'"+htmlEscape(g_presets[i].label)+"'"; }
  html += "];var froms=[";
  for (int i=0;i<g_presetCount;i++) { if (i) html += ","; html += "'"+htmlEscape(g_presets[i].from)+"'"; }
  html += "];var tos=[";
  for (int i=0;i<g_presetCount;i++) { if (i) html += ","; html += "'"+htmlEscape(g_presets[i].to)+"'"; }
  html += "];"
         "if(i>=0 && i<L){"
           "document.getElementById('pid').value=i;"
           "document.getElementById('plabel').value=labels[i];"
           "document.getElementById('pfrom').value=froms[i];"
           "document.getElementById('pto').value=tos[i];"
           "window.scrollTo({top:0,behavior:'smooth'});"
         "}"
       "}"
    "</script>";
  return html;
}

static String buildRoutePage(const String& ipStr, const String& notice) {
  String page =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Route Setup</title>"
    "<style>"
    "body{font-family:sans-serif;background:#111;color:#eee;padding:24px}"
    "input,button{font-size:18px;padding:10px;margin:6px;width:100%;box-sizing:border-box}"
    "form{max-width:820px;margin:0 auto}"
    "label{display:block;margin-top:10px}"
    ".box{background:#1c1c1c;padding:16px;border-radius:8px;margin-bottom:20px}"
    ".grid2{display:grid;grid-template-columns:1fr;gap:10px}"
    "@media(min-width:720px){.grid2{grid-template-columns:1fr 1fr}}"
    ".hint{color:#aaa;font-size:14px}"
    ".notice{background:#0b5;color:#031;padding:10px 12px;border-radius:8px;margin:12px 0;display:" + String(notice.length()? "block":"none") + "}"
    ".list .item{display:flex;justify-content:space-between;align-items:center;background:#222;padding:8px;border-radius:8px;margin:8px 0}"
    ".list .itxt span{color:#aaa;font-size:14px}"
    ".list .iact form,.list .iact button{display:inline-block;width:auto;margin:4px}"
    ".danger{background:#6b1b1b;color:#fff;border:0;border-radius:8px}"
    "button{background:#2b2b2b;border:0;border-radius:8px;color:#eee;cursor:pointer}"
    "button:active{transform:scale(0.99)}"
    "</style></head><body>"
    "<h2>Configura percorso</h2>"
    "<div class='box'>"
    "<div id='notice' class='notice'>" + notice + "</div>"
    "<form id='rform' method='POST' action='/route'>"
    "<input type='hidden' name='action' value='save_route'>"
    "<div class='grid2'>"
      "<div><label>Partenza</label><input id='from' name='from' value='" + htmlEscape(g_from) + "' placeholder='Es. Bellinzona'></div>"
      "<div><label>Arrivo</label><input id='to' name='to' value='" + htmlEscape(g_to) + "' placeholder='Es. Lugano'></div>"
    "</div>"
    "<label>Refresh (secondi, 60–3600)</label>"
    "<input id='refresh' type='number' min='60' max='3600' step='10' name='refresh' value='" + String(g_refreshSec) + "'>"
    "<button type='submit'>Salva percorso</button>"
    "</form>"
    "<p class='hint'>Tip: tap singolo sul display cicla i preset salvati, senza modificare l’NVS.</p>"
    "<p class='hint'>IP pannello: <b>" + ipStr + "</b></p>"
    "</div>";
  page += buildPresetsHTML();
  page += "<script>setTimeout(function(){var n=document.getElementById('notice'); if(n){n.style.display='none';}},2200);</script></body></html>";
  return page;
}

// =========================== Touch helpers ====================
static void initTouch(bool showOverlay) {
  (void)showOverlay; // parametro mantenuto per compatibilità
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setClock(400000);
#if (TOUCH_RST >= 0)
  pinMode(TOUCH_RST, OUTPUT); digitalWrite(TOUCH_RST, LOW);
#endif
#if (TOUCH_INT >= 0)
  pinMode(TOUCH_INT, INPUT_PULLUP);
#endif
  delay(10);
#if (TOUCH_RST >= 0)
  digitalWrite(TOUCH_RST, HIGH);
#endif
  delay(50);
  ts.begin();
  delay(30);
  g_touchLastActivityMs = millis();
}

static bool touchTapDetected() {
  ts.read();
  uint32_t now = millis();
  bool pressedNow = (ts.touches > 0) || ts.isTouched;

  if (pressedNow) g_touchLastActivityMs = now; else if ((now - g_touchLastActivityMs) > TOUCH_REINIT_MS) { initTouch(false); g_touchLastActivityMs = now; }

  if (pressedNow) {
    if (!g_touchDown) {
      g_touchDown   = true; g_touchDownMs = now; g_touchMovedTooFar = false;
      if (ts.touches > 0) { g_touchStartX = ts.points[0].x; g_touchStartY = ts.points[0].y; } else { g_touchStartX = g_touchStartY = -1; }
    } else {
      if (ts.touches > 0 && g_touchStartX >= 0) {
        int16_t dx = (int16_t)ts.points[0].x - g_touchStartX;
        int16_t dy = (int16_t)ts.points[0].y - g_touchStartY;
        if ((dx < -TAP_MOVE_TOL_PX) || (dx > TAP_MOVE_TOL_PX) || (dy < -TAP_MOVE_TOL_PX) || (dy > TAP_MOVE_TOL_PX)) g_touchMovedTooFar = true;
      }
    }
    return false; // decisione al rilascio
  }

  if (g_touchDown) {
    g_touchDown = false; g_lastReleaseMs = now;
    uint32_t dur = now - g_touchDownMs; bool durOK = (dur >= TAP_MIN_MS && dur <= TAP_MAX_MS); bool moveOK = !g_touchMovedTooFar;
    if (!(durOK && moveOK)) return false;
    return false; // validazione differita dopo quiet time
  }

  if ((g_lastReleaseMs != 0) && ((now - g_lastReleaseMs) >= TAP_QUIET_MS)) {
    if ((now - g_lastTapMs) >= TAP_COOLDOWN_MS) { g_lastTapMs = now; g_lastReleaseMs = 0; return true; }
    g_lastReleaseMs = 0;
  }
  return false;
}

static void applyPresetByIndex(int idx) {
  if (idx < 0 || idx >= g_presetCount) return;
  g_from = g_presets[idx].from;
  g_to   = g_presets[idx].to;
  buildApiUrl();
  forceRedrawNow();
}

// =========================== Fetch & redraw ===================
static uint32_t lastHeaderUpdate = 0;
static void doFetchAndRedraw() {
  drawStatus("Aggiornamento...");
  Row rows[ROWS_MAX]; int n = 0;
  bool ok = fetchDepartures(rows, n);
  if (ok) {
    clearContentArea();
    drawSubHeader();
    for (int i = 0; i < n; ++i) {
      uint16_t col = rows[i].delayMin > 0 ? RGB565_RED : RGB565_WHITE;
      gfx->setTextColor(col);
      drawRow(i, rows[i].timeHHMM, rows[i].line, rows[i].duration, rows[i].transfers);
    }
  } else {
    if (WiFi.status() != WL_CONNECTED) drawStatus("NET ERR: Wi-Fi"); else drawStatus("HTTP/JSON ERR");
  }
  g_lastFetchMs = millis();
}

// =========================== Setup / loop =====================
void setup() {
  Serial.begin(115200);
  delay(50);
  backlightOn();
  panelKickstart();

  loadRouteFromNVS();
  loadPresetsFromNVS();

  if (!tryConnectSTA()) { startAPWithPortal(); }
  else { registerWebHandlers(false); }

  initTouch(true);
  syncTimeOnce();

  gfx->fillScreen(RGB565_BLACK);
  drawHeader();
  drawRouteBar();
  drawSubHeader();
  lastHeaderUpdate = millis();

  doFetchAndRedraw();
}

void loop() {
  if (WiFi.getMode() == WIFI_AP) dnsServer.processNextRequest();
  web.handleClient();

  if (touchTapDetected()) {
    if (g_presetCount <= 0) {
      gfx->fillRect(0, HEADER_H, 480, ROUTE_H, RGB565_RED);
      gfx->setTextSize(2);
      gfx->setTextColor(RGB565_WHITE, RGB565_RED);
      gfx->setCursor(16, HEADER_H + 2);
      gfx->print("Nessun preset salvato");
    } else {
      if (g_touchPresetIdx < 0) g_touchPresetIdx = 0; else g_touchPresetIdx = (g_touchPresetIdx + 1) % g_presetCount;
      gfx->fillRect(0, HEADER_H, 480, ROUTE_H, RGB565_RED);
      gfx->setTextSize(2);
      gfx->setTextColor(RGB565_WHITE, RGB565_RED);
      gfx->setCursor(16, HEADER_H + 2);
      String lbl = g_presets[g_touchPresetIdx].label;
      String preview = lbl.length() ? ("Preset: " + lbl) : ("Preset: " + g_presets[g_touchPresetIdx].from + " -> " + g_presets[g_touchPresetIdx].to);
      gfx->print(preview);
      applyPresetByIndex(g_touchPresetIdx);
    }
  }

  if (!g_timeSynced || millis() - lastHeaderUpdate >= 30000) {
    syncTimeOnce();
    drawHeader();
    drawRouteBar();
    drawSubHeader();
    lastHeaderUpdate = millis();
  }

  if (g_routeChanged) {
    g_routeChanged = false;
    drawHeader();
    drawRouteBar();
    drawSubHeader();
    doFetchAndRedraw();
  }

  if (millis() - g_lastFetchMs >= g_refreshSec * 1000UL) { doFetchAndRedraw(); }
  delay(5);
}
