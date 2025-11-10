/* ============================================================================
   Gat Multi Ticker – ESP32-S3 Panel-4848S040 (NO TOUCH)

   SCOPO
   -----
   Mostra a rotazione su pannello ST7701 (480x480) varie pagine di informazioni:
   - Meteo (oggi + prossimi 3 giorni) da wttr.in (JSON j1)
   - Qualità dell’aria (pm2_5, pm10, ozone, nitrogen_dioxide) da Open-Meteo
   - Orologio (digitale grande)
   - Calendario ICS remoto (solo eventi della giornata locale)
   - Prezzo BTC in CHF da CoinGecko
   - Frase del giorno da ZenQuotes
   - Info dispositivo (rete, memoria, uptime, ecc.)
   - Countdown multipli (fino a 8)
   - Collegamento (transport.opendata.ch): prossima connessione tra 2 stazioni

   NOVITÀ
   ------
   - WebUI: sezione “Pagine visibili” con checkbox per abilitare/disabilitare
     ogni pagina. La rotazione salta automaticamente le pagine disattivate.
     Le scelte sono persistenti (NVS) tramite una bitmask.

   ========================================================================== */

#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>
#include <time.h>
#include <math.h>

// =========================== Display / PWM ==================================
#define GFX_BL 38
#define PWM_CHANNEL 0
#define PWM_FREQ 1000
#define PWM_BITS 8

Arduino_DataBus* bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED /*DC*/, 39 /*CS*/,
  48 /*SCK*/, 47 /*MOSI*/, GFX_NOT_DEFINED /*MISO*/
);

Arduino_ESP32RGBPanel* rgbpanel = new Arduino_ESP32RGBPanel(
  18, 17, 16, 21,       // DE, VSYNC, HSYNC, PCLK
  11, 12, 13, 14, 0,    // R0..R4
  8, 20, 3, 46, 9, 10,  // G0..G5
  4, 5, 6, 7, 15,       // B0..B4
  1, 10, 8, 50,         // HSYNC: pol, fp, pw, bp
  1, 10, 8, 20,         // VSYNC: pol, fp, pw, bp
  0,                    // PCLK neg edge
  12000000,             // 12 MHz
  false, 0, 0, 0);

Arduino_RGB_Display* gfx = new Arduino_RGB_Display(
  480, 480,
  rgbpanel, 0, true,
  bus, GFX_NOT_DEFINED,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations));

// THEME
#define COL_BG        0x0008
#define COL_HEADER    0x780F
#define COL_TEXT      0xFFFF
#define COL_SUBTEXT   0xC618
#define COL_DIVIDER   0xFFE0
#define COL_ACCENT1   0xFFFF
#define COL_ACCENT2   0x07FF
#define COL_GOOD      0x07E0
#define COL_WARN      0xFFE0
#define COL_BAD       0xF800

// =========================== Layout =========================================
static const int HEADER_H = 50;
static const int PAGE_X = 16;
static const int PAGE_Y = HEADER_H + 12;
static const int PAGE_W = 480 - 32;
static const int PAGE_H = 480 - PAGE_Y - 16;

static const int BASE_CHAR_W = 6;
static const int BASE_CHAR_H = 8;
static const int TEXT_SCALE = 2;
static const int CHAR_W = BASE_CHAR_W * TEXT_SCALE;
static const int CHAR_H = BASE_CHAR_H * TEXT_SCALE;

static const int ITEMS_LINES_SP = 6;

// =========================== NTP ============================================
static const char* NTP_SERVER = "pool.ntp.org";
static const long GMT_OFFSET_SEC = 3600;
static const int  DAYLIGHT_OFFSET_SEC = 3600;
static bool g_timeSynced = false;

static bool waitForValidTime(uint32_t timeoutMs = 8000) {
  uint32_t t0 = millis(); time_t now = 0; struct tm info;
  while ((millis() - t0) < timeoutMs) {
    time(&now); localtime_r(&now, &info);
    if (info.tm_year + 1900 > 2020) return true;
    delay(100);
  }
  return false;
}
static void syncTimeFromNTP() {
  if (g_timeSynced) return;
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  g_timeSynced = waitForValidTime(8000);
}
static String getFormattedDateTime() {
  if (!g_timeSynced) return "";
  time_t now; struct tm t; time(&now); localtime_r(&now, &t);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d/%02d - %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
  return String(buf);
}
static void todayYMD(String &ymd) {
  time_t now; struct tm t; time(&now); localtime_r(&now, &t);
  char buf[16]; snprintf(buf, sizeof(buf), "%04d%02d%02d", t.tm_year+1900, t.tm_mon+1, t.tm_mday);
  ymd = buf;
}

// =========================== Wi-Fi / Web ====================================
Preferences prefs;
DNSServer dnsServer;
WebServer web(80);

String sta_ssid, sta_pass;
String ap_ssid, ap_pass;
const byte DNS_PORT = 53;

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
  gfx->fillScreen(COL_BG);
}

// =========================== Sanificazione testo ============================
static String sanitizeText(const String& in) {
  String s = in;
  s.replace("\xC2\xA0"," ");
  s.replace("à","a"); s.replace("À","A"); s.replace("è","e"); s.replace("È","E");
  s.replace("é","e"); s.replace("É","E"); s.replace("ì","i"); s.replace("Ì","I");
  s.replace("ò","o"); s.replace("Ò","O"); s.replace("ù","u"); s.replace("Ù","U");
  s.replace("\xE2\x80\x98","'"); s.replace("\xE2\x80\x99","'");
  s.replace("\xE2\x80\x9C","'"); s.replace("\xE2\x80\x9D","'");
  s.replace("\xE2\x80\x9E","'"); s.replace("\xE2\x80\x9F","'"); s.replace("\xC2\xAB","'"); s.replace("\xC2\xBB","'");
  s.replace("`","'"); s.replace("\xC2\xB4","'");
  s.replace("\xC2\xB0",""); s.replace("\xE2\x88\x97","*"); s.replace("\xE2\x80\x93","-"); s.replace("\xE2\x80\x94","-");
  s.replace("\xE2\x80\xA2","*"); s.replace("\xC2\xB5","u"); s.replace("\xE2\x82\x82","2"); s.replace("\xE2\x82\x83","3"); s.replace("\xC2\xB3","3");
  s.replace("&nbsp;"," "); s.replace("&amp;","&"); s.replace("&lt;","<"); s.replace("&gt;",">"); s.replace("&quot;","'"); s.replace("&apos;","'");
  s.trim(); while (s.indexOf("  ") >= 0) s.replace("  ", " ");
  return s;
}
static String clip15(const String& s) {
  String t = sanitizeText(s);
  if (t.length() <= 15) return t;
  return t.substring(0, 15) + "...";
}

// =========================== UI: Testo ======================================
static void drawBoldTextColored(int16_t x, int16_t y, const String& raw, uint16_t fg, uint16_t bg) {
  String s = sanitizeText(raw);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x+1, y);   gfx->print(s);
  gfx->setCursor(x,   y+1); gfx->print(s);
  gfx->setCursor(x+1, y+1); gfx->print(s);
  gfx->setCursor(x,   y);   gfx->print(s);
}
static void drawBoldMain(int16_t x, int16_t y, const String& raw) { drawBoldTextColored(x, y, raw, COL_TEXT, COL_BG); }

static void drawCenteredBold(int16_t y, const String& raw, uint8_t scale, uint16_t fg, uint16_t bg) {
  String s = sanitizeText(raw);
  int w = s.length() * BASE_CHAR_W * scale;
  int x = (480 - w) / 2;
  gfx->setTextSize(scale);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x+1, y);   gfx->print(s);
  gfx->setCursor(x,   y+1); gfx->print(s);
  gfx->setCursor(x+1, y+1); gfx->print(s);
  gfx->setCursor(x,   y);   gfx->print(s);
  gfx->setTextSize(TEXT_SCALE);
}

static void drawHeader(const String& titleRaw) {
  gfx->fillRect(0, 0, 480, HEADER_H, COL_HEADER);
  String title = sanitizeText(titleRaw);
  drawBoldTextColored(16, 20, title, COL_TEXT, COL_HEADER);
  String dt = getFormattedDateTime();
  if (dt.length()) {
    int tw = dt.length() * CHAR_W;
    drawBoldTextColored(480 - tw - 16, 20, dt, COL_ACCENT1, COL_HEADER);
  }
}
static void drawHLine(int y) { gfx->drawLine(PAGE_X, y, PAGE_X + PAGE_W, y, COL_DIVIDER); }

static void drawParagraph(int16_t x, int16_t y, int16_t w, const String& text, uint8_t scale) {
  int maxChars = w / (BASE_CHAR_W * scale);
  if (maxChars < 8) maxChars = 8;
  gfx->setTextSize(scale);
  gfx->setTextColor(COL_TEXT, COL_BG);
  String s = sanitizeText(text);
  int start = 0;
  while (start < (int)s.length()) {
    int len = min(maxChars, (int)s.length() - start);
    int cut = start + len;
    if (cut < (int)s.length()) {
      int lastSpace = s.lastIndexOf(' ', cut);
      if (lastSpace > start) cut = lastSpace;
    }
    String line = s.substring(start, cut);
    line.trim();
    gfx->setCursor(x, y);
    gfx->print(line);
    y += BASE_CHAR_H * scale + 6;
    start = (cut < (int)s.length() && s.charAt(cut) == ' ') ? cut + 1 : cut;
    if (y > 470) break;
  }
}

// =========================== Storage impostazioni ============================
static String g_city = "Bellinzona";
static String g_lang = "it";
static String g_ics  = "";
static String g_lat  = "";
static String g_lon  = "";
static uint32_t PAGE_INTERVAL_MS = 15000;

static String g_from_station = "Bellinzona";
static String g_to_station   = "Lugano";

struct CDEvent { String name; String whenISO; };
static CDEvent cd[8];

// ======= PAGINE / ROTAZIONE =================================================
enum Page {
  P_WEATHER=0, P_AIR=1, P_CLOCK=2, P_CAL=3, P_BTC=4, P_QOD=5, P_INFO=6, P_COUNT=7, P_PT=8, PAGES=9
};
static int g_page = 0;

// Array di visibilità pagina (persistente via bitmask).
static bool g_show[PAGES] = { true,true,true,true,true,true,true,true,true };

// Helpers mask <-> array
static uint16_t pagesMaskFromArray() {
  uint16_t m = 0;
  for (int i=0;i<PAGES;i++) if (g_show[i]) m |= (1u<<i);
  return m;
}
static void pagesArrayFromMask(uint16_t m) {
  for (int i=0;i<PAGES;i++) g_show[i] = (m & (1u<<i)) != 0;
  // Safety: se tutte false, abilita almeno l'orologio
  bool any=false; for (int i=0;i<PAGES;i++) if (g_show[i]) { any=true; break; }
  if (!any) { for (int i=0;i<PAGES;i++) g_show[i]=false; g_show[P_CLOCK]=true; }
}

// Trova la prima pagina abilitata; -1 se nessuna.
static int firstEnabledPage() {
  for (int i=0;i<PAGES;i++) if (g_show[i]) return i;
  return -1;
}
// Avanza g_page alla prossima pagina abilitata (wrap); ritorna false se nessuna.
static bool advanceToNextEnabled() {
  if (firstEnabledPage() < 0) return false;
  for (int k=0;k<PAGES;k++) {
    g_page = (g_page + 1) % PAGES;
    if (g_show[g_page]) return true;
  }
  return false;
}
// Se la pagina corrente è disabilitata, spostati sulla prima abilitata.
static void ensureCurrentPageEnabled() {
  if (g_show[g_page]) return;
  int f = firstEnabledPage();
  if (f >= 0) g_page = f;
  else { // impossibile: fallback
    g_page = P_CLOCK;
    for (int i=0;i<PAGES;i++) g_show[i]=false;
    g_show[P_CLOCK]=true;
  }
}

// =========================== HTTP helpers ===================================
static inline bool isHttpOk(int code) { return code >= 200 && code < 300; }
static bool httpGET(const String& url, String& out, uint32_t timeout=8000) {
  HTTPClient http; http.setTimeout(timeout);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (!isHttpOk(code)) { http.end(); return false; }
  out = http.getString(); http.end(); return true;
}
static int indexOfCI(const String& s, const String& pat, int from=0) {
  String S = s; S.toLowerCase(); String P = pat; P.toLowerCase(); return S.indexOf(P, from);
}

// =========================== Meteo (wttr.in) ================================
static float w_now_tempC = NAN;
static String w_now_desc = "";
static float w_minC[3] = {NAN,NAN,NAN};
static float w_maxC[3] = {NAN,NAN,NAN};
static String w_desc[3] = {"","",""};

static bool jsonFindStringKV(const String& body, const String& key, int from, String& outVal) {
  String k = String("\"") + key + String("\"");
  int p = body.indexOf(k, from); if (p<0) return false;
  p = body.indexOf(':', p); if (p<0) return false;
  int q1 = body.indexOf('"', p+1); if (q1<0) return false;
  int q2 = body.indexOf('"', q1+1); if (q2<0) return false;
  outVal = body.substring(q1+1, q2);
  return true;
}
static bool fetchWeather() {
  String cityURL = g_city; cityURL.trim(); cityURL.replace(" ", "%20");
  String url = String("https://wttr.in/") + cityURL + "?format=j1&lang=" + g_lang;
  String body; if (!httpGET(url, body, 12000)) return false;

  int cc = indexOfCI(body, "\"current_condition\"");
  if (cc >= 0) {
    String tmp;
    if (jsonFindStringKV(body, "temp_C", cc, tmp)) w_now_tempC = tmp.toFloat();

    String lkey = String("\"lang_") + g_lang + String("\"");
    int wl = indexOfCI(body, lkey, cc);
    if (wl >= 0) {
      String loc;
      if (jsonFindStringKV(body, "value", wl, loc)) w_now_desc = loc;
    } else {
      int wd = indexOfCI(body, "\"weatherDesc\"", cc);
      if (wd >= 0) {
        int val = indexOfCI(body, "\"value\"", wd);
        if (val >= 0) jsonFindStringKV(body, "value", val, w_now_desc);
      }
    }
    w_now_desc = clip15(w_now_desc);
  }

  int wpos = indexOfCI(body, "\"weather\"");
  for (int d = 0; d < 3; d++) {
    if (wpos < 0) break;
    int dpos = (d == 0) ? body.indexOf('{', wpos) : body.indexOf('{', wpos + 1);
    if (dpos < 0) break;

    int depth = 0, i = dpos, endBrace = -1;
    for (; i < (int)body.length(); ++i) {
      char c = body.charAt(i);
      if (c == '{') depth++;
      else if (c == '}') { depth--; if (depth == 0) { endBrace = i; break; } }
    }
    if (endBrace < 0) break;
    String blk = body.substring(dpos, endBrace + 1);

    String smin, smax;
    if (jsonFindStringKV(blk, "mintempC", 0, smin)) w_minC[d] = smin.toFloat();
    if (jsonFindStringKV(blk, "maxtempC", 0, smax)) w_maxC[d] = smax.toFloat();

    w_desc[d] = "";
    String lkey = String("\"lang_") + g_lang + String("\"");
    int wl = indexOfCI(blk, lkey);
    if (wl >= 0) {
      String loc;
      if (jsonFindStringKV(blk, "value", wl, loc)) w_desc[d] = loc;
    }
    if (!w_desc[d].length()) {
      int wd = indexOfCI(blk, "\"weatherDesc\"");
      if (wd >= 0) {
        String val;
        if (jsonFindStringKV(blk, "value", wd, val)) w_desc[d] = val;
      }
    }
    w_desc[d] = clip15(w_desc[d]);
    wpos = endBrace + 1;
  }

  return true;
}

// =========================== Qualità Aria (Open-Meteo) ======================
static float aq_pm25 = NAN, aq_pm10 = NAN, aq_o3 = NAN, aq_no2 = NAN;

static bool extractObjectBlock(const String& body, const String& key, String& out) {
  int k = indexOfCI(body, String("\"") + key + String("\""));
  if (k < 0) return false;
  int b = body.indexOf('{', k);
  if (b < 0) return false;
  int depth = 0;
  for (int i = b; i < (int)body.length(); ++i) {
    char c = body.charAt(i);
    if (c == '{') depth++;
    else if (c == '}') { depth--; if (depth == 0) { out = body.substring(b, i + 1); return true; } }
  }
  return false;
}
static bool parseFirstNumberList(const String& obj, const String& key, float *out, int /*want*/) {
  int p = indexOfCI(obj, String("\"") + key + String("\""));
  if (p < 0) return false;
  int arr = obj.indexOf('[', p);
  if (arr < 0) return false;
  int s = arr + 1;
  while (s < (int)obj.length() && (obj.charAt(s) == ' ' || obj.charAt(s) == '\n' || obj.charAt(s) == '\r' || obj.charAt(s) == '\t')) s++;
  if (s < (int)obj.length() && obj.charAt(s) == '"') return false;
  int e = s;
  while (e < (int)obj.length()) {
    char c = obj.charAt(e);
    if (c == ',' || c == ']') break;
    e++;
  }
  if (e <= s) return false;
  String num = obj.substring(s, e);
  num.trim();
  out[0] = num.toFloat();
  return true;
}
static bool geocodeIfNeeded() {
  if (g_lat.length() && g_lon.length()) return true;
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&name=" + g_city + "&language=" + g_lang;
  String body; if (!httpGET(url, body)) return false;
  int p = indexOfCI(body, "\"latitude\""); if (p < 0) return false;
  int s = body.indexOf(':', p); if (s<0) return false;
  int e = body.indexOf(',', s+1); if (e<0) e = body.length();
  String lat = sanitizeText(body.substring(s+1, e));
  p = indexOfCI(body, "\"longitude\""); if (p<0) return false;
  s = body.indexOf(':', p); if (s<0) return false;
  e = body.indexOf(',', s+1); if (e<0) e = body.length();
  String lon = sanitizeText(body.substring(s+1, e));
  if (!lat.length() || !lon.length()) return false;
  g_lat = lat; g_lon = lon; saveAppConfig();
  return true;
}
static bool fetchAir() {
  if (!geocodeIfNeeded()) return false;
  String url = "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + g_lat +
               "&longitude=" + g_lon +
               "&hourly=pm2_5,pm10,ozone,nitrogen_dioxide&timezone=auto";
  String body; if (!httpGET(url, body)) return false;

  String hourlyBlk;
  if (!extractObjectBlock(body, "hourly", hourlyBlk)) return false;

  float tmp[1];
  if (parseFirstNumberList(hourlyBlk, "pm2_5", tmp, 1)) aq_pm25 = tmp[0]; else aq_pm25 = NAN;
  if (parseFirstNumberList(hourlyBlk, "pm10",  tmp, 1)) aq_pm10 = tmp[0]; else aq_pm10 = NAN;
  if (parseFirstNumberList(hourlyBlk, "ozone", tmp, 1)) aq_o3   = tmp[0]; else aq_o3   = NAN;
  if (parseFirstNumberList(hourlyBlk, "nitrogen_dioxide", tmp, 1)) aq_no2 = tmp[0]; else aq_no2 = NAN;

  return true;
}
static int catFrom(float v, float a, float b, float c, float d) {
  if (isnan(v)) return -1;
  if (v <= a) return 0; if (v <= b) return 1; if (v <= c) return 2; if (v <= d) return 3; return 4;
}
static const char* catLabel(int c) {
  switch(c){case 0:return "Buona, ";case 1:return "discreta, ";case 2:return "moderata, ";case 3:return "scadente, ";default:return "molto scadente, ";}
}
static String airVerdict() {
  int worst = -1;
  int c;
  c = catFrom(aq_pm25,10,20,25,50); if (c>worst) worst=c;
  c = catFrom(aq_pm10,20,40,50,100); if (c>worst) worst=c;
  c = catFrom(aq_o3,  80,120,180,240); if (c>worst) worst=c;
  c = catFrom(aq_no2, 40, 90,120,230); if (c>worst) worst=c;
  if (worst<0) return "Aria: n/d";
  String msg = String("Aria ") + catLabel(worst);
  if (worst==0) msg += " aria pulita";
  else if (worst==1) msg += " nessun problema rilevante";
  else if (worst==2) msg += " attenzione per soggetti sensibili";
  else if (worst==3) msg += " limita attivita all'aperto se possibile";
  else msg += " evita attivita all'aperto";
  return msg;
}

// ====================== Calendario ICS (solo OGGI) ==========================
struct CalItem { String when; String summary; String where; time_t ts; bool allDay; };
CalItem cal[3];

static String trimField(const String& s) { String t=s; t.trim(); return t; }
static void resetCal() {
  for (int i=0;i<3;i++){
    cal[i].when=""; cal[i].summary=""; cal[i].where="";
    cal[i].ts=0; cal[i].allDay=false;
  }
}
static String extractAfterColon(const String& src, int pos){
  int c=src.indexOf(':',pos); if(c<0) return "";
  int e=src.indexOf('\n', c+1); if(e<0) e=src.length();
  return trimField(src.substring(c+1,e));
}
static bool isTodayStamp(const String& dtstampYmd, const String& todayYmd){
  if (dtstampYmd.length() < 8) return false;
  return dtstampYmd.substring(0,8) == todayYmd;
}
static void humanTimeFromStamp(const String& stamp, String &out){
  if (stamp.length()>=15 && stamp.charAt(8)=='T') {
    String hh = stamp.substring(9,11);
    String mm = stamp.substring(11,13);
    out = hh + ":" + mm;
  } else out = "tutto il giorno";
}
static bool fetchICS() {
  resetCal();
  if (!g_ics.length()) return true;

  String body;
  if (!httpGET(g_ics, body, 15000)) return false;

  String today; todayYMD(today);

  int idx=0;
  int p=0;
  while (idx<3) {
    int b = body.indexOf("BEGIN:VEVENT", p); if (b<0) break;
    int e = body.indexOf("END:VEVENT", b); if (e<0) break;
    String blk = body.substring(b, e);

    int ds = indexOfCI(blk, "DTSTART");
    String rawStart="";
    if (ds>=0) rawStart = extractAfterColon(blk, ds);
    if (!rawStart.length()) { p = e + 10; continue; }

    String ymd = rawStart.substring(0,8);
    if (!isTodayStamp(ymd, today)) { p = e + 10; continue; }

    int ss = indexOfCI(blk, "SUMMARY"); String summary="";
    if (ss>=0) summary = extractAfterColon(blk, ss);
    int ls = indexOfCI(blk, "LOCATION"); String where="";
    if (ls>=0) where = extractAfterColon(blk, ls);

    String when; humanTimeFromStamp(rawStart, when);

    struct tm tt = {};
    tt.tm_year = rawStart.substring(0,4).toInt() - 1900;
    tt.tm_mon  = rawStart.substring(4,6).toInt() - 1;
    tt.tm_mday = rawStart.substring(6,8).toInt();
    bool hasTime = (rawStart.length()>=15 && rawStart.charAt(8)=='T');
    if (hasTime) {
      tt.tm_hour = rawStart.substring(9,11).toInt();
      tt.tm_min  = rawStart.substring(11,13).toInt();
      tt.tm_sec  = 0;
    } else {
      tt.tm_hour = 0; tt.tm_min = 0; tt.tm_sec = 0;
    }
    time_t evt_ts = mktime(&tt);

    if (summary.length()) {
      cal[idx].when    = sanitizeText(when);
      cal[idx].summary = sanitizeText(summary);
      cal[idx].where   = sanitizeText(where);
      cal[idx].ts      = evt_ts;
      cal[idx].allDay  = !hasTime;
      idx++;
    }
    p = e + 10;
  }
  return true;
}

// =========================== Bitcoin CHF ====================================
static double btc_chf = NAN;
static uint32_t btc_last_ok_ms = 0;

static bool jsonFindNumberKV(const String& body, const String& key, int from, double &outVal) {
  String k = String("\"") + key + String("\"");
  int p = body.indexOf(k, from); if (p < 0) return false;
  p = body.indexOf(':', p); if (p < 0) return false;
  int s = p + 1;
  while (s < (int)body.length() && (body[s]==' '||body[s]=='\n'||body[s]=='\r'||body[s]=='\t')) s++;
  int e = s;
  while (e < (int)body.length()) {
    char c = body[e];
    if (c==',' || c=='}' || c==']' || c==' ') break;
    e++;
  }
  if (e <= s) return false;
  String num = body.substring(s, e);
  num.trim();
  outVal = num.toDouble();
  return true;
}
static String formatCHF(double v) {
  if (isnan(v)) return String("--.--");
  long long intPart = (long long)llround(floor(v));
  int cents = (int)llround((v - floor(v)) * 100.0);
  if (cents == 100) { intPart += 1; cents = 0; }
  String sInt = String(intPart);
  String out = ""; int count = 0;
  for (int i = sInt.length()-1; i >= 0; --i) {
    out = sInt.charAt(i) + out;
    count++;
    if (count == 3 && i > 0) { out = String("'") + out; count = 0; }
  }
  if (cents == 0) return out;
  char buf[16]; snprintf(buf, sizeof(buf), "%02d", cents);
  return out + String(".") + String(buf);
}
static bool fetchBTC() {
  String url = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=chf";
  String body; if (!httpGET(url, body, 10000)) return false;
  int b = indexOfCI(body, "\"bitcoin\""); if (b < 0) return false;
  double val = NAN; if (!jsonFindNumberKV(body, "chf", b, val)) return false;
  btc_chf = val; btc_last_ok_ms = millis(); return true;
}

// =========================== Frase del giorno ================================
static String qod_text = "";
static String qod_author = "";

static bool fetchQOD() {
  String body; if (!httpGET("https://zenquotes.io/api/today", body, 10000)) return false;
  String q, a;
  if (!jsonFindStringKV(body, "q", 0, q)) return false;
  jsonFindStringKV(body, "a", 0, a);
  qod_text = sanitizeText(q);
  qod_author = sanitizeText(a);
  if (qod_text.length() > 280) qod_text = qod_text.substring(0, 277) + "...";
  return true;
}
static void pageQOD() {
  drawHeader("Frase del giorno");
  int y = PAGE_Y;

  if (!qod_text.length()) {
    drawBoldMain(PAGE_X, y + CHAR_H, g_lang=="it" ? "Nessuna frase disponibile" : "No quote available");
    return;
  }

  uint8_t quoteScale = 2;
  if (qod_text.length() < 80)       quoteScale = 4;
  else if (qod_text.length() < 160) quoteScale = 3;

  drawParagraph(PAGE_X, y, PAGE_W, String("“") + qod_text + String("”"), quoteScale);

  String author = qod_author.length() ? String("- ") + qod_author : String("— sconosciuto");
  uint8_t authorScale = 2;
  if (author.length() < 18)       authorScale = 3;
  else if (author.length() < 28)  authorScale = 2;

  int authorY = PAGE_Y + PAGE_H - (BASE_CHAR_H * authorScale) - 8;
  int authorW = author.length() * BASE_CHAR_W * authorScale;
  int authorX = (480 - authorW) / 2;

  gfx->setTextSize(authorScale);
  gfx->setTextColor(COL_ACCENT1, COL_BG);
  gfx->setCursor(authorX+1, authorY);   gfx->print(author);
  gfx->setCursor(authorX,   authorY+1); gfx->print(author);
  gfx->setCursor(authorX+1, authorY+1); gfx->print(author);
  gfx->setCursor(authorX,   authorY);   gfx->print(author);
  gfx->setTextSize(TEXT_SCALE);
}

// =========================== Trasporti pubblici ==============================
static String pt_dep_time = "";
static String pt_line     = "";
static String pt_dur_comp = "";
static int    pt_transfers = -1;

static bool jsonFindArrayFirstString(const String& body, const String& key, int from, String& outVal) {
  String k = String("\"") + key + String("\"");
  int p = body.indexOf(k, from); if (p<0) return false;
  int a = body.indexOf('[', p); if (a<0) return false;
  int q1 = body.indexOf('"', a+1); if (q1<0) return false;
  int q2 = body.indexOf('"', q1+1); if (q2<0) return false;
  outVal = body.substring(q1+1, q2);
  return outVal.length() > 0;
}
static bool jsonFindIntKV(const String& body, const String& key, int from, int &outVal) {
  String k = String("\"") + key + String("\"");
  int p = body.indexOf(k, from); if (p<0) return false;
  p = body.indexOf(':', p); if (p<0) return false;
  int s = p + 1;
  while (s < (int)body.length() && isspace((int)body[s])) s++;
  int e = s;
  while (e < (int)body.length() && isdigit((int)body[e])) e++;
  if (e <= s) return false;
  outVal = body.substring(s, e).toInt();
  return true;
}
static String isoToHM(const String& iso) {
  int t = iso.indexOf('T');
  if (t > 0 && iso.length() >= t+5+1) {
    String hh = iso.substring(t+1, t+3);
    String mm = iso.substring(t+4, t+6);
    return hh + ":" + mm;
  }
  return String("--:--");
}
static String compactDuration(const String& dur) {
  int d_sep = dur.indexOf('d');
  int days = 0;
  int off = 0;
  if (d_sep > 0) {
    days = dur.substring(0, d_sep).toInt();
    off = d_sep + 1;
  }
  if ((int)dur.length() < off + 7) return dur;

  int h = dur.substring(off + 0, off + 2).toInt();
  int m = dur.substring(off + 3, off + 5).toInt();
  h += days * 24;
  if (h <= 0) return String(m) + "m";
  char buf[16];
  snprintf(buf, sizeof(buf), "%dh%02dm", h, m);
  return String(buf);
}
static bool extractFirstJourneyLine(const String& connObj, String& outLine) {
  int sec = indexOfCI(connObj, "\"sections\"");
  if (sec < 0) return false;
  int arr = connObj.indexOf('[', sec); if (arr < 0) return false;
  int j = indexOfCI(connObj, "\"journey\"", arr);
  if (j < 0) return false;
  String cat, num;
  jsonFindStringKV(connObj, "category", j, cat);
  jsonFindStringKV(connObj, "number",   j, num);
  String line = "";
  if (cat.length()) line += cat;
  if (num.length()) {
    if (line.length()) line += " ";
    line += num;
  }
  if (!line.length()) return false;
  outLine = sanitizeText(line);
  return true;
}
static bool fetchPT() {
  pt_dep_time = ""; pt_line = ""; pt_dur_comp = ""; pt_transfers = -1;
  if (!g_from_station.length() || !g_to_station.length()) return false;

  auto enc = [](String s) {
    String out; out.reserve(s.length() * 3);
    for (int i = 0; i < (int)s.length(); ++i) {
      uint8_t c = (uint8_t)s[i];
      bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c=='-' || c=='_' || c=='.' || c=='~';
      if (safe) out += (char)c;
      else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); out += b; }
    }
    return out;
  };

  String url = "https://transport.opendata.ch/v1/connections?limit=1&from=" + enc(g_from_station) +
               "&to=" + enc(g_to_station);

  String body;
  if (!httpGET(url, body, 12000)) return false;

  int cpos = indexOfCI(body, "\"connections\"");
  if (cpos < 0) return false;

  int firstObj = body.indexOf('{', cpos);
  if (firstObj < 0) return false;

  int depth = 0, endObj = -1;
  for (int i = firstObj; i < (int)body.length(); ++i) {
    char ch = body.charAt(i);
    if (ch == '{') depth++;
    else if (ch == '}') { depth--; if (depth == 0) { endObj = i; break; } }
  }
  if (endObj < 0) return false;
  String conn = body.substring(firstObj, endObj + 1);

  String depISO;
  int fromIdx = indexOfCI(conn, "\"from\"");
  if (fromIdx >= 0) jsonFindStringKV(conn, "departure", fromIdx, depISO);
  if (depISO.length()) pt_dep_time = isoToHM(depISO);

  String durRaw;
  if (jsonFindStringKV(conn, "duration", 0, durRaw)) pt_dur_comp = compactDuration(durRaw);

  int tr = -1; if (jsonFindIntKV(conn, "transfers", 0, tr)) pt_transfers = tr;

  String prod0;
  if (jsonFindArrayFirstString(conn, "products", 0, prod0)) {
    pt_line = sanitizeText(prod0);
  } else {
    String jline;
    if (extractFirstJourneyLine(conn, jline)) pt_line = jline;
  }

  return (pt_dep_time.length() + pt_dur_comp.length() + pt_line.length()) > 0;
}
static void pagePT() {
  drawHeader("Prossima partenza");

  int y = PAGE_Y;

  String route = sanitizeText(g_from_station) + "/" + sanitizeText(g_to_station);
  uint8_t routeScale = 3;
  if (route.length() > 18) routeScale = 2;
  drawCenteredBold(y, route, routeScale, COL_TEXT, COL_BG);
  y += (BASE_CHAR_H * routeScale) + 10;

  drawHLine(y);
  y += 12;

  String depLine = String("Partenza ") + (pt_dep_time.length() ? pt_dep_time : "--:--");
  uint8_t depScale = 3;
  drawCenteredBold(y, depLine, depScale, COL_TEXT, COL_BG);
  y += (BASE_CHAR_H * depScale) + 10;

  String lineLbl = pt_line.length() ? pt_line : String("--");
  uint8_t lineScale = 6;
  if (lineLbl.length() > 10) lineScale = 5;
  if (lineLbl.length() > 14) lineScale = 4;
  drawCenteredBold(y, lineLbl, lineScale, COL_ACCENT1, COL_BG);
  y += (BASE_CHAR_H * lineScale) + 12;

  drawHLine(y);
  y += 12;
}

// =========================== Pagine display =================================
static void pageBTC() {
  drawHeader("Valore Bitcoin");
  int y = PAGE_Y;

  String price = isnan(btc_chf) ? String("--.--") : formatCHF(btc_chf);

  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(6);
  String line = String("CHF ") + price;
  int tw = (int)line.length() * BASE_CHAR_W * 6;
  gfx->setCursor((480 - tw)/2, y + 80);
  gfx->print(line);

  gfx->setTextSize(2);
  y += 160;
  drawHLine(y); y += 12;
  String sub = "Fonte: CoinGecko   |   Aggiornato ";
  if (btc_last_ok_ms) {
    unsigned long secs = (millis() - btc_last_ok_ms) / 1000UL;
    sub += String(secs) + "s fa";
  } else {
    sub += "n/d";
  }
  drawBoldMain(PAGE_X, y + CHAR_H, sub);
  gfx->setTextSize(TEXT_SCALE);
}

static void pageWeather() {
  drawHeader(String("Meteo per ") + sanitizeText(g_city));
  int y = PAGE_Y;

  String line1;
  if (!isnan(w_now_tempC) && w_now_desc.length()) {
    line1 = String((int)round(w_now_tempC)) + "C  |  " + w_now_desc;
  } else {
    line1 = (g_lang=="it" ? "Dati non disponibili" : "Data not available");
  }
  drawBoldMain(PAGE_X, y + CHAR_H, line1); y += CHAR_H*2 + ITEMS_LINES_SP;
  drawHLine(y); y += 10;

  for (int i=0;i<3;i++) {
    String row = (g_lang=="it" ? "Giorno +" : "Day ") + String(i+1) + ": ";
    if (!isnan(w_minC[i]) && !isnan(w_maxC[i])) {
      row += String((int)round(w_minC[i])) + "-" + String((int)round(w_maxC[i])) + "C ->";
    } else {
      row += (g_lang=="it" ? "n/d" : "n/a");
    }
    if (w_desc[i].length()) row += String(" ") + w_desc[i];
    drawBoldMain(PAGE_X, y + CHAR_H, row);
    y += CHAR_H*2 + 4;
  }
}
static void pageAir() {
  drawHeader(String("Aria a ") + sanitizeText(g_city));
  int y = PAGE_Y;

  String r1 = String("PM2.5: ") + (isnan(aq_pm25) ? "--" : String(aq_pm25, 1)) + " ug/m3";
  String r2 = String("PM10 : ") + (isnan(aq_pm10) ? "--" : String(aq_pm10, 1)) + " ug/m3";
  String r3 = String("O3   : ") + (isnan(aq_o3)   ? "--" : String(aq_o3, 0))   + " ug/m3";
  String r4 = String("NO2  : ") + (isnan(aq_no2)  ? "--" : String(aq_no2, 0))  + " ug/m3";

  drawBoldMain(PAGE_X, y + CHAR_H, r1); y += CHAR_H*2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, r2); y += CHAR_H*2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, r3); y += CHAR_H*2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, r4); y += CHAR_H*2 + 10;

  drawHLine(y); y += 10;

  String verdict = airVerdict();
  verdict.replace("aria pulita", "ottimo!");
  verdict.replace("nessun problema rilevante", "tutto ok.");
  verdict.replace("attenzione per soggetti sensibili", "moderata.");
  verdict.replace("limita attivita all'aperto se possibile", "è scadente...");
  verdict.replace("evita attivita all'aperto", "presta attenzione!");

  if (verdict.length() > 46) verdict = verdict.substring(0, 46) + "...";

  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print(sanitizeText(verdict));
}
static void pageClock() {
  time_t now; struct tm t; time(&now); localtime_r(&now, &t);
  String greeting;
  int minutes = t.tm_hour * 60 + t.tm_min;
  if      (minutes < 360)   greeting = "Buonanotte!";
  else if (minutes < 690)   greeting = "Buongiorno!";
  else if (minutes < 840)   greeting = "Buon appetito!";
  else if (minutes < 1080)  greeting = "Buon pomeriggio!";
  else if (minutes < 1320)  greeting = "Buonasera!";
  else                      greeting = "Buonanotte!";
  drawHeader(greeting);

  char bufH[3]; snprintf(bufH, sizeof(bufH), "%02d", t.tm_hour);
  char bufM[3]; snprintf(bufM, sizeof(bufM), "%02d", t.tm_min);
  char bufD[24]; snprintf(bufD, sizeof(bufD), "%02d/%02d/%04d", t.tm_mday, t.tm_mon+1, t.tm_year+1900);

  const int timeScale = 16;
  const int dateScale = 5;
  const int chW = BASE_CHAR_W * timeScale;
  const int chH = BASE_CHAR_H * timeScale;

  const int timeTop = PAGE_Y + 28;
  const int totalW  = 5 * chW;
  const int timeX   = (480 - totalW) / 2;

  gfx->fillRect(PAGE_X, timeTop - 8, PAGE_W, chH + 36 + (BASE_CHAR_H * dateScale) + 12 + 16, COL_BG);

  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(timeScale);
  gfx->setCursor(timeX, timeTop);
  gfx->print(bufH);
  gfx->setCursor(timeX + (3 * chW), timeTop);
  gfx->print(bufM);

  {
    const int colonX = timeX + (2 * chW);
    const int colonY = timeTop;
    const int colonW = chW;
    gfx->fillRect(colonX, colonY, colonW, chH, COL_BG);
    int dotW = max(4, chW / 6);
    int dotH = max(4, chH / 10);
    int dotX = colonX + (colonW - dotW) / 2;
    int upperY = colonY + (int)(chH * 0.32f);
    int lowerY = colonY + (int)(chH * 0.68f) - dotH;
    gfx->fillRect(dotX, upperY, dotW, dotH, COL_ACCENT1);
    gfx->fillRect(dotX, lowerY, dotW, dotH, COL_ACCENT1);
  }

  int sepY = timeTop + chH + 12;
  drawHLine(sepY);

  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(dateScale);
  int dw = BASE_CHAR_W * dateScale * (int)strlen(bufD);
  int dx = (480 - dw) / 2;
  int dy = sepY + 22;
  gfx->setCursor(dx, dy);
  gfx->print(bufD);
  gfx->setTextSize(TEXT_SCALE);
}
static void pageCalendar() {
  drawHeader("Calendario (oggi)");
  int y = PAGE_Y;

  struct Row { String when, summary, where; time_t ts; bool allDay; long delta; } rows[3];
  int n = 0;

  time_t now; time(&now);
  for (int i = 0; i < 3; i++) {
    if (!cal[i].summary.length()) continue;

    long d;
    if (cal[i].allDay) d = 0;
    else {
      d = (long)difftime(cal[i].ts, now);
      if (d < 0) d = 24L*3600L;
    }

    rows[n].when = cal[i].when;
    rows[n].summary = cal[i].summary;
    rows[n].where = cal[i].where;
    rows[n].ts = cal[i].ts;
    rows[n].allDay = cal[i].allDay;
    rows[n].delta = d;
    n++;
  }

  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (rows[j].delta < rows[i].delta) {
        Row t = rows[i]; rows[i] = rows[j]; rows[j] = t;
      }
    }
  }

  if (n == 0) {
    drawBoldMain(PAGE_X, y + CHAR_H, (g_lang=="it" ? "Nessun evento" : "No events today"));
    return;
  }

  for (int i = 0; i < n; i++) {
    drawBoldMain(PAGE_X, y + CHAR_H, rows[i].summary);
    y += CHAR_H*2;

    if (rows[i].when.length())  { gfx->setTextSize(1); gfx->setTextColor(COL_TEXT, COL_BG); gfx->setCursor(PAGE_X, y); gfx->print(rows[i].when); y+= 16; }
    if (rows[i].where.length()) { gfx->setTextSize(1); gfx->setTextColor(COL_TEXT, COL_BG); gfx->setCursor(PAGE_X, y); gfx->print(rows[i].where); y+= 16; }
    gfx->setTextSize(TEXT_SCALE);

    y += 6;
    if (i < n - 1) { drawHLine(y); y += 10; }
    if (y > 460) break;
  }
}
static String formatBytes(size_t b) {
  if (b >= (1<<20)) return String(b / (1<<20)) + " MB";
  if (b >= (1<<10)) return String(b / (1<<10)) + " KB";
  return String(b) + " B";
}
static String macStr() {
  uint8_t m[6]; WiFi.macAddress(m);
  char buf[24];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0],m[1],m[2],m[3],m[4],m[5]);
  return String(buf);
}
static String formatShortDate(time_t t) {
  struct tm lt; localtime_r(&t, &lt);
  char buf[16]; snprintf(buf, sizeof(buf), "%02d/%02d", lt.tm_mday, lt.tm_mon + 1);
  return String(buf);
}
static String formatUptime() {
  unsigned long ms = millis();
  unsigned long sec = ms / 1000UL;
  unsigned int d = sec / 86400UL; sec %= 86400UL;
  unsigned int h = sec / 3600UL;  sec %= 3600UL;
  unsigned int m = sec / 60UL;    sec %= 60UL;
  char buf[48];
  snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02lu", d, h, m, sec);
  return String(buf);
}
static void pageInfo() {
  drawHeader("Info dispositivo");
  int y = PAGE_Y;

  bool sta = (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED);
  String mode = (WiFi.getMode() == WIFI_AP) ? "AP" : (sta ? "STA" : "Idle");

  String ip     = sta ? WiFi.localIP().toString() : (WiFi.getMode()==WIFI_AP ? WiFi.softAPIP().toString() : "n/d");
  String gw     = sta ? WiFi.gatewayIP().toString() : "n/d";
  String ssid   = sta ? WiFi.SSID() : (WiFi.getMode()==WIFI_AP ? ap_ssid : "n/d");
  int    chan   = sta ? WiFi.channel() : 0;

  drawBoldMain(PAGE_X, y + CHAR_H, String("Mode: ") + mode); y += CHAR_H*2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("SSID: ") + sanitizeText(ssid)); y += CHAR_H*2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("IP  : ") + ip + String("   GW: ") + gw); y += CHAR_H*2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("MAC : ") + macStr() + String("   CH: ") + String(chan)); y += CHAR_H*2 + 4;

  drawHLine(y); y += 10;

  drawBoldMain(PAGE_X, y + CHAR_H, String("Uptime   : ") + formatUptime()); y += CHAR_H*2 + 4;

  size_t ps = ESP.getPsramSize();
  if (ps > 0) { drawBoldMain(PAGE_X, y + CHAR_H, String("PSRAM    : ") + formatBytes(ps)); y += CHAR_H*2 + 4; }

  drawBoldMain(PAGE_X, y + CHAR_H, String("Flash    : ") + formatBytes(ESP.getFlashChipSize())); y += CHAR_H*2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("Sketch   : ") + formatBytes(ESP.getSketchSize()) + " / " + formatBytes(ESP.getFreeSketchSpace())); y += CHAR_H*2 + 4;

  drawBoldMain(PAGE_X, y + CHAR_H, String("CPU      : ") + String(ESP.getCpuFreqMHz()) + " MHz"); y += CHAR_H*2 + 4;
}

// =========================== Countdown ======================================
static bool parseISOToTimeT(const String& iso, time_t &out) {
  if (!iso.length() || iso.length() < 16) return false;
  struct tm t = {};
  t.tm_year = iso.substring(0,4).toInt() - 1900;
  t.tm_mon  = iso.substring(5,7).toInt() - 1;
  t.tm_mday = iso.substring(8,10).toInt();
  t.tm_hour = iso.substring(11,13).toInt();
  t.tm_min  = iso.substring(14,16).toInt();
  t.tm_sec  = 0;
  out = mktime(&t); return out > 0;
}
static String formatDelta(time_t target) {
  if (!g_timeSynced) return String("n/d");
  time_t now; time(&now);
  long diff = (long)difftime(target, now);
  if (diff <= 0) return String(g_lang=="it"?"scaduto":"expired");
  long d = diff / 86400L; diff %= 86400L;
  long h = diff / 3600L;  diff %= 3600L;
  long m = diff / 60L;
  char buf[64];
  if (d>0) snprintf(buf, sizeof(buf), "%ldg %02ldh %02ldm", d, h, m);
  else     snprintf(buf, sizeof(buf), "%02ldh %02ldm", h, m);
  return String(buf);
}
static void pageCountdowns() {
  drawHeader("Countdown");
  int y = PAGE_Y;
  bool any = false;

  struct CDTemp { String name; time_t when; } list[8];

  int n = 0;
  for (int i = 0; i < 8; i++) {
    if (!cd[i].name.length() || !cd[i].whenISO.length()) continue;
    time_t t;
    if (!parseISOToTimeT(cd[i].whenISO, t)) continue;
    list[n].name = cd[i].name;
    list[n].when = t;
    n++;
  }

  for (int i = 0; i < n - 1; i++)
    for (int j = i + 1; j < n; j++)
      if (difftime(list[i].when, list[j].when) > 0) { CDTemp tmp = list[i]; list[i] = list[j]; list[j] = tmp; }

  for (int i = 0; i < n; i++) {
    time_t now; time(&now);
    if (list[i].when <= now) continue;
    any = true;

    String dtStr = formatShortDate(list[i].when);
    String left  = formatDelta(list[i].when);
    String row   = sanitizeText(list[i].name) + String(" (") + dtStr + String(") ") + left;

    drawBoldMain(PAGE_X, y + CHAR_H, row);
    y += CHAR_H * 2 + 6;

    if (i < n - 1) { drawHLine(y); y += 8; }
    if (y > 460) break;
  }

  if (!any) drawBoldMain(PAGE_X, y + CHAR_H, (g_lang == "it" ? "Nessun countdown configurato" : "No countdowns"));
}

// =========================== Dati / Refresh =================================
static uint32_t lastRefresh = 0;
static const uint32_t REFRESH_MS = 10UL*60UL*1000UL;
static uint32_t lastPageSwitch = 0;
volatile bool g_dataRefreshPending = false;

static void drawCurrentPage() {
  ensureCurrentPageEnabled();
  gfx->fillScreen(COL_BG);
  switch (g_page) {
    case P_WEATHER: if (g_show[P_WEATHER]) pageWeather(); else { /*no-op*/ } break;
    case P_AIR:     if (g_show[P_AIR])     pageAir();     break;
    case P_CLOCK:   if (g_show[P_CLOCK])   pageClock();   break;
    case P_CAL:     if (g_show[P_CAL])     pageCalendar();break;
    case P_BTC:     if (g_show[P_BTC])     pageBTC();     break;
    case P_QOD:     if (g_show[P_QOD])     pageQOD();     break;
    case P_INFO:    if (g_show[P_INFO])    pageInfo();    break;
    case P_COUNT:   if (g_show[P_COUNT])   pageCountdowns(); break;
    case P_PT:      if (g_show[P_PT])      pagePT();      break;
  }
}
static void refreshAll() {
  if (g_show[P_WEATHER]) fetchWeather();
  if (g_show[P_AIR])     fetchAir();
  if (g_show[P_CAL])     fetchICS();
  if (g_show[P_BTC])     fetchBTC();
  if (g_show[P_QOD])     fetchQOD();
  if (g_show[P_PT])      fetchPT();
  drawCurrentPage();
  lastPageSwitch = millis();
}

// =========================== Captive + WebUI ================================
static String htmlAP() {
  String ip = WiFi.softAPIP().toString();
  String page =
    "<!doctype html><html><head>"
    "<meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>Wi-Fi Setup</title>"
    "<style>body{font-family:system-ui,Segoe UI,Roboto,Ubuntu,Arial,sans-serif}input{width:280px}label{display:block;margin-top:10px}</style>"
    "</head><body>"
    "<h2>Configura Wi-Fi</h2>"
    "<form method='POST' action='/save'>"
    "<label>SSID</label><input name='ssid'/>"
    "<label>Password</label><input name='pass' type='password'/>"
    "<p><button type='submit'>Salva & Connetti</button></p>"
    "</form>"
    "<p>Se il popup non compare, apri <b>http://" + ip + "/</b></p>"
    "</body></html>";
  return page;
}

// ------- HTML SETTINGS (aggiunta sezione Pagine visibili) -------------------
static String checkbox(const char* name, bool checked, const char* label) {
  return String("<label style='display:flex;gap:8px;align-items:center'><input type='checkbox' name='") + name +
         "' value='1' " + (checked?"checked":"") + "/><span>"+label+"</span></label>";
}

static String htmlSettings(bool saved=false, const String& msg="") {
  String notice = saved ? "<div class='ok'>Impostazioni salvate. Aggiornamento in corso…</div>" :
                 (msg.length()? ("<div class='warn'>" + msg + "</div>") : "");
  uint32_t page_s = PAGE_INTERVAL_MS / 1000;
  String page =
    "<!doctype html><html><head>"
    "<meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>Impostazioni</title>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Helvetica,Arial,sans-serif;margin:0;background:#0b0b0b;color:#eee}"
    "header{position:sticky;top:0;background:#0b5bd3;padding:14px 16px;color:#ffea00;box-shadow:0 1px 8px rgba(0,0,0,.3)}"
    "main{padding:16px;max-width:900px;margin:0 auto}"
    ".card{background:#141414;border:1px solid #262626;border-radius:12px;box-shadow:0 4px 14px rgba(0,0,0,.25);padding:14px 16px}"
    "label{display:block;margin-top:10px;color:#ddd}"
    "input{width:100%;padding:10px 12px;border:1px solid #2b2b2b;background:#0f0f0f;color:#eee;border-radius:8px;outline:none}"
    "input:focus{border-color:#0dad4a;box-shadow:0 0 0 3px rgba(13,173,74,.2)}"
    ".row{display:flex;gap:12px;flex-wrap:wrap}"
    ".row>div{flex:1;min-width:240px}"
    ".btn{appearance:none;border:none;border-radius:10px;padding:10px 14px;font-weight:600;cursor:pointer}"
    ".primary{background:#0dad4a;color:#111}"
    ".ghost{background:transparent;color:#ddd;border:1px solid #2b2b2b}"
    ".ok{margin:12px 0;padding:12px 14px;border-left:4px solid #0dad4a;background:#0f2218;color:#c7ffd9;border-radius:8px}"
    ".warn{margin:12px 0;padding:12px 14px;border-left:4px solid #ad6b0d;background:#221a0f;color:#ffe2c7;border-radius:8px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:8px}"
    "</style></head><body>"
    "<header><h1>Impostazioni</h1></header><main>"
    + notice +
    "<div class='card'>"
    "<form method='POST' action='/settings'>"
    "<div class='row'>"
    "<div><label>Citta</label><input name='city' value='" + sanitizeText(g_city) + "' placeholder='Bellinzona'/></div>"
    "<div><label>Lingua (it/en)</label><input name='lang' value='" + sanitizeText(g_lang) + "' placeholder='it'/></div>"
    "</div>"
    "<label>URL Calendario ICS (opzionale)</label>"
    "<input name='ics' value='" + sanitizeText(g_ics) + "' placeholder='https://.../calendar.ics'/>"
    "<div class='row'>"
    "<div><label>Tempo cambio pagina (secondi)</label><input name='page_s' type='number' min='5' max='600' value='" + String(page_s) + "'/></div>"
    "</div>"
    "</div>"
    "<br>"
    "<div class='card'>"
    "<h3>Collegamento (transport.opendata.ch)</h3>"
    "<div class='row'>"
    "<div><label>Partenza</label><input name='pt_from' value='" + sanitizeText(g_from_station) + "' placeholder='Bellinzona'/></div>"
    "<div><label>Arrivo</label><input name='pt_to' value='" + sanitizeText(g_to_station) + "' placeholder='Lugano'/></div>"
    "</div>"
    "</div>"
    "<br>"
    "<div class='card'>"
    "<h3>Pagine visibili</h3>"
    "<div class='grid'>"
      + checkbox("p_WEATHER", g_show[P_WEATHER], "Meteo (wttr.in)") +
      checkbox("p_AIR",      g_show[P_AIR],      "Qualità aria (Open-Meteo)") +
      checkbox("p_CLOCK",    g_show[P_CLOCK],    "Orologio") +
      checkbox("p_CAL",      g_show[P_CAL],      "Calendario ICS (oggi)") +
      checkbox("p_BTC",      g_show[P_BTC],      "Bitcoin in CHF") +
      checkbox("p_QOD",      g_show[P_QOD],      "Frase del giorno") +
      checkbox("p_INFO",     g_show[P_INFO],     "Info dispositivo") +
      checkbox("p_COUNT",    g_show[P_COUNT],    "Countdown") +
      checkbox("p_PT",       g_show[P_PT],       "Prossima partenza") +
    "</div>"
    "<p style='opacity:.8;margin-top:8px'>Se le deselezioni tutte, l'Orologio resterà comunque attivo.</p>"
    "</div>"
    "<br>"
    "<div class='card'>"
    "<h3>Countdown eventi (max 8)</h3>"
    "<div class='row'>"
    "<div><label>Nome #1</label><input name='cd1n' value='" + sanitizeText(cd[0].name) + "' placeholder='Evento 1'/></div>"
    "<div><label>Data/Ora #1</label><input name='cd1t' type='datetime-local' value='" + sanitizeText(cd[0].whenISO) + "'/></div>"
    "</div>"
    "<div class='row'>"
    "<div><label>Nome #2</label><input name='cd2n' value='" + sanitizeText(cd[1].name) + "' placeholder='Evento 2'/></div>"
    "<div><label>Data/Ora #2</label><input name='cd2t' type='datetime-local' value='" + sanitizeText(cd[1].whenISO) + "'/></div>"
    "</div>"
    "<div class='row'>"
    "<div><label>Nome #3</label><input name='cd3n' value='" + sanitizeText(cd[2].name) + "' placeholder='Evento 3'/></div>"
    "<div><label>Data/Ora #3</label><input name='cd3t' type='datetime-local' value='" + sanitizeText(cd[2].whenISO) + "'/></div>"
    "</div>"
    "<div class='row'>"
    "<div><label>Nome #4</label><input name='cd4n' value='" + sanitizeText(cd[3].name) + "' placeholder='Evento 4'/></div>"
    "<div><label>Data/Ora #4</label><input name='cd4t' type='datetime-local' value='" + sanitizeText(cd[3].whenISO) + "'/></div>"
    "</div>"
    "<div class='row'>"
    "<div><label>Nome #5</label><input name='cd5n' value='" + sanitizeText(cd[4].name) + "' placeholder='Evento 5'/></div>"
    "<div><label>Data/Ora #5</label><input name='cd5t' type='datetime-local' value='" + sanitizeText(cd[4].whenISO) + "'/></div>"
    "</div>"
    "<div class='row'>"
    "<div><label>Nome #6</label><input name='cd6n' value='" + sanitizeText(cd[5].name) + "' placeholder='Evento 6'/></div>"
    "<div><label>Data/Ora #6</label><input name='cd6t' type='datetime-local' value='" + sanitizeText(cd[5].whenISO) + "'/></div>"
    "</div>"
    "<div class='row'>"
    "<div><label>Nome #7</label><input name='cd7n' value='" + sanitizeText(cd[6].name) + "' placeholder='Evento 7'/></div>"
    "<div><label>Data/Ora #7</label><input name='cd7t' type='datetime-local' value='" + sanitizeText(cd[6].whenISO) + "'/></div>"
    "</div>"
    "<div class='row'>"
    "<div><label>Nome #8</label><input name='cd8n' value='" + sanitizeText(cd[7].name) + "' placeholder='Evento 8'/></div>"
    "<div><label>Data/Ora #8</label><input name='cd8t' type='datetime-local' value='" + sanitizeText(cd[7].whenISO) + "'/></div>"
    "</div>"
    "<p style='margin-top:14px'><button class='btn primary' type='submit'>Salva</button> "
    "<a class='btn ghost' href='/'>Home</a></p>"
    "</form>"
    "</div>"
    "</main></body></html>";
  return page;
}

// -------------------- Handlers AP/STA ---------------------------------------
static void handleRootAP() { web.send(200, "text/html; charset=utf-8", htmlAP()); }
static void handleSave() {
  if (web.hasArg("ssid") && web.hasArg("pass")) {
    prefs.begin("wifi", false);
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    prefs.end();
    web.send(200, "text/html; charset=utf-8",
      "<!doctype html><meta charset='utf-8'><body><h3>Salvate. Mi connetto…</h3>"
      "<script>setTimeout(()=>{fetch('/reboot')},800);</script></body>");
  } else {
    web.send(400, "text/plain; charset=utf-8", "Bad Request");
  }
}
static void handleReboot(){ web.send(200, "text/plain; charset=utf-8", "OK"); delay(100); ESP.restart(); }

static String htmlHome() {
  uint32_t s = PAGE_INTERVAL_MS / 1000;
  String enabledList = "";
  const char* names[PAGES] = {"Meteo","Aria","Orologio","Calendario","BTC","Frase","Info","Countdown","Partenza"};
  for (int i=0;i<PAGES;i++) { if (g_show[i]) { if (enabledList.length()) enabledList += ", "; enabledList += names[i]; } }
  String page = "<!doctype html><html><head><meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>Gat Multi Ticker</title>"
    "<style>body{font-family:system-ui,Segoe UI,Roboto,Ubuntu,Arial,sans-serif}</style>"
    "</head><body><h2>Gat Multi Ticker</h2>";
  page += String("<p><b>Citta:</b> ") + sanitizeText(g_city) + String("<br><b>Lingua:</b> ") + sanitizeText(g_lang) +
          String("<br><b>Intervallo cambio pagina:</b> ") + String(s) + String(" s</p>");
  page += String("<p><b>Pagine attive:</b> ") + enabledList + "</p>";
  page += String("<p><b>Collegamento:</b> ") + sanitizeText(g_from_station) + " → " + sanitizeText(g_to_station) + "</p>";
  page += "<p><a href='/settings'>Impostazioni</a></p></body></html>";
  return page;
}
static void handleRootSTA() { web.send(200, "text/html; charset=utf-8", htmlHome()); }

static void handleSettings() {
  bool saved = false; String msg="";
  if (web.method() == HTTP_POST) {
    String city = web.arg("city"); String lang = web.arg("lang"); String ics = web.arg("ics");
    city.trim(); lang.trim(); ics.trim();
    if (city.length()) g_city = city;
    if (lang != "it" && lang != "en") { msg = "Lingua non valida. Uso 'it'."; g_lang = "it"; }
    else g_lang = lang;
    g_ics = ics;

    if (web.hasArg("page_s")) {
      long ps = web.arg("page_s").toInt();
      if (ps < 5) ps = 5; else if (ps > 600) ps = 600;
      PAGE_INTERVAL_MS = (uint32_t)ps * 1000UL;
    }

    g_from_station = sanitizeText(web.arg("pt_from"));
    g_to_station   = sanitizeText(web.arg("pt_to"));

    cd[0].name = sanitizeText(web.arg("cd1n")); cd[0].whenISO = sanitizeText(web.arg("cd1t"));
    cd[1].name = sanitizeText(web.arg("cd2n")); cd[1].whenISO = sanitizeText(web.arg("cd2t"));
    cd[2].name = sanitizeText(web.arg("cd3n")); cd[2].whenISO = sanitizeText(web.arg("cd3t"));
    cd[3].name = sanitizeText(web.arg("cd4n")); cd[3].whenISO = sanitizeText(web.arg("cd4t"));
    cd[4].name = sanitizeText(web.arg("cd5n")); cd[4].whenISO = sanitizeText(web.arg("cd5t"));
    cd[5].name = sanitizeText(web.arg("cd6n")); cd[5].whenISO = sanitizeText(web.arg("cd6t"));
    cd[6].name = sanitizeText(web.arg("cd7n")); cd[6].whenISO = sanitizeText(web.arg("cd7t"));
    cd[7].name = sanitizeText(web.arg("cd8n")); cd[7].whenISO = sanitizeText(web.arg("cd8t"));

    // ---- Leggi checkbox pagine (presenti -> true, assenti -> false) ----
    g_show[P_WEATHER] = web.hasArg("p_WEATHER");
    g_show[P_AIR]     = web.hasArg("p_AIR");
    g_show[P_CLOCK]   = web.hasArg("p_CLOCK");
    g_show[P_CAL]     = web.hasArg("p_CAL");
    g_show[P_BTC]     = web.hasArg("p_BTC");
    g_show[P_QOD]     = web.hasArg("p_QOD");
    g_show[P_INFO]    = web.hasArg("p_INFO");
    g_show[P_COUNT]   = web.hasArg("p_COUNT");
    g_show[P_PT]      = web.hasArg("p_PT");

    // Safety: se tutte false, abilita almeno l'orologio
    bool any=false; for (int i=0;i<PAGES;i++) if (g_show[i]) { any=true; break; }
    if (!any) g_show[P_CLOCK] = true;

    // Reset coords se cambia città
    prefs.begin("app", true);
    String prevCity = prefs.getString("city", g_city);
    prefs.end();
    if (prevCity != g_city) { g_lat = ""; g_lon = ""; }

    // Persisti tutto incluso mask pagine
    prefs.begin("app", false);
    prefs.putString("city", g_city);
    prefs.putString("lang", g_lang);
    prefs.putString("ics",  g_ics);
    prefs.putString("lat",  g_lat);
    prefs.putString("lon",  g_lon);
    prefs.putULong("page_ms", PAGE_INTERVAL_MS);
    prefs.putString("pt_from", g_from_station);
    prefs.putString("pt_to",   g_to_station);
    for (int i=0;i<8;i++){ char kn[6], kt[6]; snprintf(kn,sizeof(kn),"cd%dn",i+1); snprintf(kt,sizeof(kt),"cd%dt",i+1);
      prefs.putString(kn, cd[i].name); prefs.putString(kt, cd[i].whenISO); }
    uint16_t mask = pagesMaskFromArray();
    prefs.putUShort("pages_mask", mask);
    prefs.end();

    ensureCurrentPageEnabled();
    g_dataRefreshPending = true;
    saved = true;
  }
  web.send(200, "text/html; charset=utf-8", htmlSettings(saved));
}

static void startDNSCaptive(){ dnsServer.start(DNS_PORT, "*", WiFi.softAPIP()); }
static void startAPPortal(){
  web.on("/", HTTP_GET, handleRootAP);
  web.on("/save", HTTP_POST, handleSave);
  web.on("/reboot", HTTP_GET, handleReboot);
  web.onNotFound(handleRootAP);
  web.begin();
}
static void startSTAWeb(){
  web.on("/", HTTP_GET, handleRootSTA);
  web.on("/settings", HTTP_ANY, handleSettings);
  web.onNotFound(handleRootSTA);
  web.begin();
}

// =========================== Wi-Fi ==========================================
static void drawAPScreenOnce(const String& ssid, const String& pass) {
  gfx->fillScreen(COL_BG);
  drawBoldTextColored(16, 36, "Connettiti all'AP:", COL_TEXT, COL_BG);
  drawBoldTextColored(16, 66, ssid, COL_TEXT, COL_BG);
  gfx->setTextSize(1); gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(16, 96);  gfx->print("Password: "); gfx->print(pass);
  gfx->setCursor(16, 114); gfx->print("Captive portal automatico.");
  gfx->setCursor(16, 126); gfx->print("Se non compare, apri l'IP dell'AP.");
  gfx->setTextSize(TEXT_SCALE);
}
static void startAPWithPortal() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssidbuf[32]; snprintf(ssidbuf, sizeof(ssidbuf), "PANEL-%02X%02X", mac[4], mac[5]);
  ap_ssid = ssidbuf; ap_pass = "panelsetup";
  WiFi.persistent(false); WiFi.mode(WIFI_AP); WiFi.setSleep(false);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str()); delay(100);
  startDNSCaptive(); startAPPortal(); drawAPScreenOnce(ap_ssid, ap_pass);
}
static bool tryConnectSTA(uint32_t timeoutMs = 8000) {
  prefs.begin("wifi", true);
  sta_ssid = prefs.getString("ssid", "");
  sta_pass = prefs.getString("pass", "");
  prefs.end();
  if (sta_ssid.isEmpty()) return false;
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.setSleep(false);
  WiFi.begin(sta_ssid.c_str(), sta_pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) { delay(100); yield(); }
  return WiFi.status() == WL_CONNECTED;
}

// =========================== Load/Save Config ================================
static void loadAppConfig() {
  prefs.begin("app", true);
  g_city = prefs.getString("city", g_city);
  g_lang = prefs.getString("lang", g_lang);
  g_ics  = prefs.getString("ics",  g_ics);
  g_lat  = prefs.getString("lat",  g_lat);
  g_lon  = prefs.getString("lon",  g_lon);
  PAGE_INTERVAL_MS = prefs.getULong("page_ms", PAGE_INTERVAL_MS);

  g_from_station = prefs.getString("pt_from", g_from_station);
  g_to_station   = prefs.getString("pt_to",   g_to_station);

  cd[0].name = prefs.getString("cd1n", ""); cd[0].whenISO = prefs.getString("cd1t", "");
  cd[1].name = prefs.getString("cd2n", ""); cd[1].whenISO = prefs.getString("cd2t", "");
  cd[2].name = prefs.getString("cd3n", ""); cd[2].whenISO = prefs.getString("cd3t", "");
  cd[3].name = prefs.getString("cd4n", ""); cd[3].whenISO = prefs.getString("cd4t", "");
  cd[4].name = prefs.getString("cd5n", ""); cd[4].whenISO = prefs.getString("cd5t", "");
  cd[5].name = prefs.getString("cd6n", ""); cd[5].whenISO = prefs.getString("cd6t", "");
  cd[6].name = prefs.getString("cd7n", ""); cd[6].whenISO = prefs.getString("cd7t", "");
  cd[7].name = prefs.getString("cd8n", ""); cd[7].whenISO = prefs.getString("cd8t", "");

  uint16_t mask = prefs.getUShort("pages_mask", 0x01FF /* 9 bit a 1: tutte attive */);
  prefs.end();

  // Clamp intervallo
  uint32_t s = PAGE_INTERVAL_MS / 1000;
  if (s < 5) PAGE_INTERVAL_MS = 5000; else if (s > 600) PAGE_INTERVAL_MS = 600000;

  // Applica mask pagine
  pagesArrayFromMask(mask);
}
static void saveAppConfig() {
  prefs.begin("app", false);
  prefs.putString("city", g_city);
  prefs.putString("lang", g_lang);
  prefs.putString("ics",  g_ics);
  prefs.putString("lat",  g_lat);
  prefs.putString("lon",  g_lon);
  prefs.putULong("page_ms", PAGE_INTERVAL_MS);

  prefs.putString("pt_from", g_from_station);
  prefs.putString("pt_to",   g_to_station);

  for (int i=0;i<8;i++){ char kn[6], kt[6]; snprintf(kn,sizeof(kn),"cd%dn",i+1); snprintf(kt,sizeof(kt),"cd%dt",i+1);
    prefs.putString(kn, cd[i].name); prefs.putString(kt, cd[i].whenISO); }

  uint16_t mask = pagesMaskFromArray();
  prefs.putUShort("pages_mask", mask);
  prefs.end();
}

// =========================== Setup / Loop ===================================
void setup() {
  Serial.begin(115200);
  backlightOn(); panelKickstart();
  gfx->fillScreen(COL_BG);
  drawBoldMain(16, 36, "Avvio...");

  loadAppConfig();

  if (!tryConnectSTA(8000)) {
    startAPWithPortal();
  } else {
    startSTAWeb();
    syncTimeFromNTP();
    g_dataRefreshPending = true;
  }
}

void loop() {
  if (WiFi.getMode() == WIFI_AP) {
    dnsServer.processNextRequest(); web.handleClient(); delay(10); return;
  }

  web.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t nextTry = 0;
    if (millis() > nextTry) {
      nextTry = millis() + 5000;
      if (tryConnectSTA(5000)) { if (!g_timeSynced) syncTimeFromNTP(); g_dataRefreshPending = true; }
    }
    delay(30);
    return;
  }

  if (g_dataRefreshPending) {
    g_dataRefreshPending = false;
    refreshAll();
  }

  if (millis() - lastRefresh >= REFRESH_MS) {
    lastRefresh = millis();
    refreshAll();
  }

  if (millis() - lastPageSwitch >= PAGE_INTERVAL_MS) {
    lastPageSwitch = millis();
    // Avanza alla prossima pagina abilitata
    if (!advanceToNextEnabled()) ensureCurrentPageEnabled();
    drawCurrentPage();
  }

  delay(5);
}
