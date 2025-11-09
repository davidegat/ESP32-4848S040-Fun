/*
  Gat Panel – Departures (ESP32-S3 Panel-4848S040)

  - Fallback: Bellinzona a Lugano
  - Web UI /route: form Partenza + Arrivo -> salva in NVS, refresh immediato
  - 10 partenze, refresh ogni 2 minuti
  - Header rosso: "PARTENZE" + data/ora
  - Barra rotta (sotto header): "da <FROM>   a <TO>"
  - Colonne: ORA | LINEA | DURATA | CAMBI
  - Filtro: NON mostra partenze all'istante attuale e fino a 60s da adesso
  - Captive portal Wi-Fi (AP + DNS + form SSID/Password -> salva NVS -> reboot)
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

// =========================== Display ===========================
#define GFX_BL      38
#define PWM_CHANNEL 0
#define PWM_FREQ    1000
#define PWM_BITS    8

// =========================== NTP ===========================
static const char* NTP_SERVER = "pool.ntp.org";
static const long   GMT_OFFSET_SEC      = 3600; // CH/IT
static const int    DAYLIGHT_OFFSET_SEC = 3600;
static bool g_timeSynced = false;

// =========================== Bus / Panel =======================
Arduino_DataBus *bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /*DC*/, 39 /*CS*/,
  48 /*SCK*/, 47 /*MOSI*/, GFX_NOT_DEFINED /*MISO*/
);

Arduino_ESP32RGBPanel *rgbpanel = new Arduino_ESP32RGBPanel(
  18, 17, 16, 21,
  11, 12, 13, 14, 0,
  8, 20, 3, 46, 9, 10,
  4, 5, 6, 7, 15,
  1, 10, 8, 50,
  1, 10, 8, 20,
  0,
  12000000,
  false, 0, 0, 0
);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
  480, 480,
  rgbpanel, 0, true,
  bus, GFX_NOT_DEFINED,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// =========================== Wi-Fi / Portal ====================
DNSServer dnsServer;
WebServer  web(80);
Preferences prefs;
String sta_ssid, sta_pass, ap_ssid, ap_pass;
const byte DNS_PORT = 53;
static bool g_portalActive = false; // true quando è attivo l'AP con captive portal

// =========================== Route (config) ====================
Preferences routePrefs;
String g_from = "Bellinzona";
String g_to   = "Lugano";
String g_apiUrl;              // ricostruita da buildApiUrl()
static const int ROWS_MAX = 11;
static volatile bool g_routeChanged = false; // refresh immediato dopo POST

// =========================== Grafica ===========================
#define RGB565_RED     0xF800
#define RGB565_BLACK   0x0000
#define RGB565_WHITE   0xFFFF
#define RGB565_DKGREY  0x18E3
#define RGB565_BLUE    0x001F

static const int HEADER_H    = 56;
static const int BASE_CHAR_W = 6;
static const int BASE_CHAR_H = 8;
static const int TEXT_SCALE  = 2;
static const int CHAR_W      = BASE_CHAR_W * TEXT_SCALE;
static const int CHAR_H      = BASE_CHAR_H * TEXT_SCALE;

// Barra rotta (tra header e sub-header)
static const int ROUTE_H     = 20;

// Sub-header per colonne
static const int SUBHDR_H    = 28;

// Area contenuti
static const int CONTENT_Y   = HEADER_H + ROUTE_H + SUBHDR_H;

// Tabella
static const int ROW_H       = 34;
static const int COL1_X      = 14;    // ORA
static const int COL2_X      = 128;   // LINEA
static const int COL3_X      = 270;   // DURATA
static const int COL4_X      = 400;   // CAMBI

// =========================== Strutture =========================
struct Row {
  String timeHHMM;
  String line;
  String duration;
  String transfers;
};

// =========================== HW ================================
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

// =========================== NTP ==============================
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
  snprintf(buf, sizeof(buf), "%02d/%02d  %02d:%02d",
           ti.tm_mday, ti.tm_mon + 1, ti.tm_hour, ti.tm_min);
  return String(buf);
}

// =========================== Normalizza testo =================
static String normalize(const String& in) {
  String s = in;
  s.replace("\xC2\xA0"," ");
  s.replace("à","a"); s.replace("è","e"); s.replace("é","e");
  s.replace("ì","i"); s.replace("ò","o"); s.replace("ù","u");
  s.replace("\"","'");
  return s;
}

// =========================== Disegno ==========================
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
  drawBoldTextColored(16, 24, "PARTENZE", RGB565_WHITE, RGB565_RED);
  String datetime = fmtDateTime();
  if (datetime.length() > 0) {
    int textWidth = datetime.length() * CHAR_W;
    int xPos = 480 - textWidth - 16;
    drawBoldTextColored(xPos, 24, datetime, RGB565_WHITE, RGB565_RED);
  }
}

// Barra rotta: "da <FROM>   a <TO>" (come nel tuo sketch attuale)
static void drawRouteBar() {
  gfx->fillRect(0, HEADER_H, 480, ROUTE_H, RGB565_RED);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_RED);
  int y = HEADER_H + (ROUTE_H - 10) / 2;
  gfx->setCursor(12, y);
  String line = normalize(g_from) + " -> " + normalize(g_to);
  gfx->print(line);
}

// Sub-header: etichette colonne + separatore
static void drawSubHeader() {
  gfx->fillRect(0, HEADER_H + ROUTE_H, 480, SUBHDR_H, RGB565_BLACK);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  int baseY = HEADER_H + ROUTE_H + (SUBHDR_H - CHAR_H) / 2;
  gfx->setCursor(COL1_X, baseY); gfx->print("ORA");
  gfx->setCursor(COL2_X, baseY); gfx->print("LINEA");
  gfx->setCursor(COL3_X, baseY); gfx->print("DURATA");
  gfx->setCursor(COL4_X, baseY); gfx->print("CAMBI");
  gfx->drawLine(10, HEADER_H + ROUTE_H + SUBHDR_H - 2, 470, HEADER_H + ROUTE_H + SUBHDR_H - 2, RGB565_DKGREY);
}

static void clearContentArea() {
  gfx->fillRect(0, CONTENT_Y, 480, 480 - CONTENT_Y, RGB565_BLUE);
}

static void drawRow(int row, const String& t, const String& line, const String& dur, const String& tr) {
  int y = CONTENT_Y + 8 + row * ROW_H;
  if (row > 0) gfx->drawLine(10, y - 6, 470, y - 6, RGB565_DKGREY);

  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(RGB565_WHITE);
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

// =========================== Wi-Fi base (STA) =================
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
  while ((millis() - t0) < timeoutMs) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return false;
}

static void startAP()
{
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssidbuf[32]; snprintf(ssidbuf, sizeof(ssidbuf), "PANEL-%02X%02X", mac[4], mac[5]);
  ap_ssid = ssidbuf; ap_pass = "panelsetup";

  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
}

// ==================== Captive portal (metodo “classico”) ====================
// Utility
static String ipToString(IPAddress ip) {
  return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

// Pagina Wi-Fi (SSID/password) — semplice e leggera
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

// Handlers captive
static void handleWifiRoot() { web.send(200, "text/html", htmlWifiIndex()); }

static void handleWifiSave() {
  if (web.hasArg("ssid") && web.hasArg("pass")) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    prefs.end();
    web.send(200, "text/html",
      "<html><body><h3>Salvate. Riavvio...</h3>"
      "<script>setTimeout(()=>{fetch('/reboot')},600);</script>"
      "</body></html>");
  } else {
    web.send(400, "text/plain", "Bad Request");
  }
}

static void handleWifiReboot(){ web.send(200, "text/plain", "OK"); delay(100); ESP.restart(); }

static void startDNSCaptive(){ dnsServer.start(DNS_PORT, "*", WiFi.softAPIP()); }

// Disegno messaggio su display quando l’AP è attivo
static void drawAPScreenOnce(const String& ssid, const String& pass) {
  gfx->fillScreen(RGB565_BLACK);
  drawBoldTextColored(16, 36, "Configura rete Wi-Fi", RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(16, 74);  gfx->print("Rete: "); gfx->print(ssid);
  gfx->setCursor(16, 92);  gfx->print("Password: "); gfx->print(pass);
  gfx->setCursor(16, 110); gfx->print("Si apre un portale di configurazione.");
  gfx->setCursor(16, 124); gfx->print("Se non compare, apri http://"); gfx->print(ipToString(WiFi.softAPIP()));
  gfx->setTextSize(TEXT_SCALE);
}

// Router dei percorsi web in base alla modalita'
static void registerWebHandlers(bool portalMode) {
  web.on("/route",  HTTP_GET,  [](){ // form route sempre disponibile
    IPAddress ip = (WiFi.getMode()==WIFI_AP) ? WiFi.softAPIP() : WiFi.localIP();
    String ipStr = String(ip[0])+"."+ip[1]+"."+ip[2]+"."+ip[3];
    String page =
      "<!DOCTYPE html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>Route Setup</title>"
      "<style>"
      "body{font-family:sans-serif;background:#111;color:#eee;padding:24px}"
      "input,button{font-size:18px;padding:10px;margin:6px;width:100%;box-sizing:border-box}"
      "form{max-width:440px;margin:0 auto}"
      "label{display:block;margin-top:10px}"
      ".box{background:#1c1c1c;padding:16px;border-radius:8px}"
      "</style></head><body>"
      "<h2>Configura percorso</h2>"
      "<div class='box'>"
      "<form method='POST' action='/route'>"
      "<label>Partenza</label>"
      "<input name='from' value='" + g_from + "' placeholder='Es. Bellinzona'>"
      "<label>Arrivo</label>"
      "<input name='to' value='" + g_to + "' placeholder='Es. Lugano'>"
      "<button type='submit'>OK</button>"
      "</form>"
      "<p>IP pannello: <b>" + ipStr + "</b></p>"
      "<p>Se lasci vuoto, fallback: <b>da Bellinzona a Lugano</b>.</p>"
      "</div>"
      "</body></html>";
    web.send(200, "text/html", page);
  });

  web.on("/route", HTTP_POST, [](){
    String nf = web.hasArg("from") ? web.arg("from") : "";
    String nt = web.hasArg("to")   ? web.arg("to")   : "";
    nf.trim(); nt.trim();
    if (!nf.length()) nf = "Bellinzona";
    if (!nt.length()) nt = "Lugano";
    // salva su NVS
    routePrefs.begin("route", false);
    routePrefs.putString("from", nf);
    routePrefs.putString("to",   nt);
    routePrefs.end();
    g_from = nf; g_to = nt;
    // ricostruisci URL e forza refresh
    String fromEnc, toEnc; // (buildApiUrl() usa globali)
    buildApiUrl();
    g_routeChanged = true;

    web.send(200, "text/html",
      "<!DOCTYPE html><html><body>"
      "<h3>Impostato: da " + g_from + " a " + g_to + "</h3>"
      "<p>Aggiornamento schermo in corso.</p>"
      "<p><a href='/route'>Torna al form</a></p>"
      "</body></html>");
  });

  if (portalMode) {
    // Root = portale Wi-Fi
    web.on("/", HTTP_GET, handleWifiRoot);
    web.on("/save", HTTP_POST, handleWifiSave);
    web.on("/reboot", HTTP_GET, handleWifiReboot);
    // Qualsiasi altro path -> index Wi-Fi (per captive)
    web.onNotFound(handleWifiRoot);
  } else {
    // Root = app route
    web.on("/", HTTP_GET, [](){
      web.sendHeader("Location", "/route", true);
      web.send(302, "text/plain", "");
    });
    web.onNotFound([](){
      web.sendHeader("Location", "/route", true);
      web.send(302, "text/plain", "");
    });
  }

  web.begin();
}

// Avvio completo del captive portal (AP + DNS + handlers) e messaggio su display
static void startAPWithPortal() {
  startAP();
  startDNSCaptive();
  registerWebHandlers(true);
  g_portalActive = true;
  drawAPScreenOnce(ap_ssid, ap_pass);
}

// =========================== Utils: URL & Route ===============
// spazi -> %20 (basta per toponimi)
static String urlEncodeSpaces(const String &s) {
  String out; out.reserve(s.length()*3);
  for (size_t i=0; i<s.length(); ++i) {
    char c = s[i];
    if (c==' ') out += "%20";
    else out += c;
  }
  return out;
}

static void buildApiUrl() {
  String fromEnc = urlEncodeSpaces(g_from);
  String toEnc   = urlEncodeSpaces(g_to);
  g_apiUrl  = "https://transport.opendata.ch/v1/connections";
  // aumenta limit per avere abbastanza righe anche se si filtra la prima
  g_apiUrl += "?from=" + fromEnc + "&to=" + toEnc + "&limit=" + String(ROWS_MAX + 5);
  g_apiUrl += "&fields[]=connections/from/departure";
  g_apiUrl += "&fields[]=connections/from/departureTimestamp";
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
  String t = routePrefs.getString("to", "");
  routePrefs.end();
  if (f.length()) g_from = f;
  if (t.length()) g_to   = t;
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
  time_t tt = (time_t)ts; struct tm ti;
  localtime_r(&tt, &ti);
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
// true = da saltare
static bool isoIsThisMinute(const char* iso) {
  if (!g_timeSynced || !iso) return false;
  const char* t = strchr(iso, 'T');
  if (!t || strlen(t) < 6) return false;
  int hh = (t[1]-'0')*10 + (t[2]-'0');
  int mm = (t[4]-'0')*10 + (t[5]-'0');
  time_t now; struct tm ti; time(&now); localtime_r(&now, &ti);
  return (ti.tm_hour == hh && ti.tm_min == mm);
}

static bool shouldSkipSoon(long ts, const char* iso) {
  if (!g_timeSynced) return false;
  time_t now; time(&now);
  // scarta se la partenza è adesso o entro i prossimi 60s
  if (ts > 0 && ts <= (long)now + 60) return true;
  // fallback: se il timestamp non è affidabile, scarta se ISO è nell'HH:MM corrente
  if ((ts <= 0) && isoIsThisMinute(iso)) return true;
  return false;
}

// =========================== Transport API ====================
static bool fetchDepartures(Row outRows[ROWS_MAX], int &count)
{
  count = 0;
  if (WiFi.status() != WL_CONNECTED) return false;

  WiFiClientSecure client; client.setInsecure(); client.setTimeout(12000);
  HTTPClient http;
  if (!http.begin(client, g_apiUrl)) return false;

  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  String payload = http.getString();
  http.end();

  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, payload);
  if (err) return false;

  JsonArray conns = doc["connections"].as<JsonArray>();
  if (conns.isNull()) return false;

  for (JsonObject c : conns) {
    if (count >= ROWS_MAX) break;

    long ts = (long)(c["from"]["departureTimestamp"] | 0L);
    const char* iso = c["from"]["departure"] | nullptr;

    // salta le partenze all'istante attuale o entro 60s
    if (shouldSkipSoon(ts, iso)) continue;

    String hhmm = hhmmFromTimestamp(ts);
    if (!hhmm.length()) hhmm = hhmmFromISO(iso);
    if (!hhmm.length()) hhmm = "--:--";

    String line = extractLineLabel(c);

    // durata "0dHH:MM:SS" o "HH:MM:SS" -> minuti
    String dur = (const char*)(c["duration"] | "");
    int mm = 0;
    if (dur.length() >= 8) {
      String tail = dur.substring(dur.length() - 8);
      int h = tail.substring(0,2).toInt();
      int m = tail.substring(3,5).toInt();
      mm = h*60 + m;
    }
    int transfers = (int)(c["transfers"] | 0);

    Row r;
    r.timeHHMM = hhmm;
    r.line     = line;
    r.duration = String(mm) + "m";
    r.transfers= String(transfers);
    outRows[count++] = r;
  }
  return count > 0;
}

// =========================== Web UI (root/route) ===============
// (Gli handler sono registrati in registerWebHandlers())

// =========================== MAIN LOOP ========================
static uint32_t lastHeaderUpdate   = 0;
static uint32_t lastFetchMs        = 0;
static const uint32_t FETCH_EVERY  = 2UL * 60UL * 1000UL;

static void doFetchAndRedraw() {
  drawStatus("Aggiorno partenze...");
  Row rows[ROWS_MAX]; int n = 0;
  bool ok = fetchDepartures(rows, n);
  if (ok) {
    clearContentArea();
    drawSubHeader();
    for (int i = 0; i < n; ++i)
      drawRow(i, rows[i].timeHHMM, rows[i].line, rows[i].duration, rows[i].transfers);
  } else {
    if (WiFi.status() != WL_CONNECTED) drawStatus("NET ERR: Wi-Fi");
    else                               drawStatus("HTTP/JSON ERR");
  }
  lastFetchMs = millis();
}

void setup() {
  Serial.begin(115200);
  delay(50);
  backlightOn();
  panelKickstart();

  loadRouteFromNVS();

  // Prova STA, altrimenti captive portal
  if (!tryConnectSTA()) {
    startAPWithPortal();   // AP + DNS + /save + reboot
  } else {
    registerWebHandlers(false); // modalità normale (root -> /route)
  }

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

  // Header + barra rotta ogni ~30 s (o se NTP non ancora valido)
  if (!g_timeSynced || millis() - lastHeaderUpdate >= 30000) {
    syncTimeOnce();
    drawHeader();
    drawRouteBar();
    drawSubHeader();
    lastHeaderUpdate = millis();
  }

  // Refresh forzato dopo salvataggio dal form (aggiorna anche la barra rotta)
  if (g_routeChanged) {
    g_routeChanged = false;
    drawHeader();
    drawRouteBar();
    drawSubHeader();
    doFetchAndRedraw();
  }

  // Aggiorna tabella ogni 2 minuti
  if (millis() - lastFetchMs >= FETCH_EVERY) {
    doFetchAndRedraw();
  }

  delay(5);
}
