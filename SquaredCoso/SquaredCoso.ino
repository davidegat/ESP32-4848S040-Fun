/* ============================================================================
   Gat Multi Ticker – ESP32-S3 Panel-4848S040 (NO TOUCH)

   SCOPO
   -----
   Mostra a rotazione su pannello ST7701 (480x480) varie pagine informative:

   - Meteo: condizioni attuali + prossimi giorni (wttr.in – JSON j1)
   - Qualità dell’aria (pm2_5, pm10, ozone, nitrogen_dioxide) da Open-Meteo
   - Orologio digitale con greeting contestuale
   - Calendario ICS remoto (solo eventi del giorno locale)
   - Bitcoin in CHF da CoinGecko, con calcolo valore posseduto
   - Countdown multipli (fino a 8)
   - Connessione ferroviaria (transport.opendata.ch)
   - Quote of the Day:
       * Se configurato: usa OpenAI (model gpt-5-nano) con argomento personalizzato
       * In assenza di configurazione: fallback automatico ZenQuotes
       * Cache giornaliera per evitare richieste ripetute
   - Valute (cambio da CHF verso EUR/USD/GBP/JPY) – fonte Frankfurter.app
   - Temperature prossimi giorni (7-day forecast) da Open-Meteo,
     convertite in grafico lineare a 24 step
   - Ore di luce (alba, tramonto, durata) – Sunrise-Sunset API
   - Info dispositivo (rete, memoria, uptime)

   NOVITÀ
   ------
   - WebUI: sezione “Pagine visibili” con checkbox. La rotazione salta
     automaticamente le pagine disattivate. Le scelte sono persistenti (NVS)
     tramite bitmask.
   - WebUI: sezione “Quote of the day” con campi:
       * Chiave API OpenAI
       * Argomento della frase da generare
   - Quote of the Day: priorità OpenAI > ZenQuotes con cache giornaliera
   - Pagina Temperatura: evoluzione prossimi 7 giorni con dati Open-Meteo
============================================================================ */


#include <Arduino.h>
#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Arduino_GFX_Library.h>
#include <time.h>
#include <math.h>
#include "SquaredCoso.h"
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
#define COL_BG 0x232E
#define COL_HEADER 0x2967
#define COL_TEXT 0xFFFF
#define COL_SUBTEXT 0xC618
#define COL_DIVIDER 0xFFE0
#define COL_ACCENT1 0xFFFF
#define COL_ACCENT2 0x07FF
#define COL_GOOD 0x07E0
#define COL_WARN 0xFFE0
#define COL_BAD 0xF800

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
static double g_btc_owned = NAN;  // quantità BTC posseduti (opzionale)

// =========================== NTP ============================================
static const char* NTP_SERVER = "pool.ntp.org";
static const long GMT_OFFSET_SEC = 3600;
static const int DAYLIGHT_OFFSET_SEC = 3600;
static bool g_timeSynced = false;

static bool waitForValidTime(uint32_t timeoutMs = 8000) {
  uint32_t t0 = millis();
  time_t now = 0;
  struct tm info;
  while ((millis() - t0) < timeoutMs) {
    time(&now);
    localtime_r(&now, &info);
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
  time_t now;
  struct tm t;
  time(&now);
  localtime_r(&now, &t);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d/%02d - %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
  return String(buf);
}
static void todayYMD(String& ymd) {
  time_t now;
  struct tm t;
  time(&now);
  localtime_r(&now, &t);
  char buf[16];
  snprintf(buf, sizeof(buf), "%04d%02d%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
  ymd = buf;
}

/* ============================================================================
   UTILITY JSON 
============================================================================ */

static bool jsonFindIntKV(const String& body, const String& key, int from, int& outVal) {
  String k = "\"" + key + "\"";
  int p = body.indexOf(k, from);
  if (p < 0) return false;

  p = body.indexOf(':', p);
  if (p < 0) return false;

  int s = p + 1;
  while (s < body.length() && isspace(body[s])) s++;

  int e = s;
  while (e < body.length() && isdigit(body[e])) e++;

  if (e <= s) return false;

  outVal = body.substring(s, e).toInt();
  return true;
}

static bool jsonFindArrayFirstString(const String& body, const String& key,
                                     int from, String& outVal) {

  String k = "\"" + key + "\"";
  int p = body.indexOf(k, from);
  if (p < 0) return false;

  int a = body.indexOf('[', p);
  int q1 = body.indexOf('"', a + 1);
  int q2 = body.indexOf('"', q1 + 1);

  if (a < 0 || q1 < 0 || q2 < 0) return false;

  outVal = body.substring(q1 + 1, q2);
  return outVal.length() > 0;
}

static String isoToHM(const String& iso) {
  int t = iso.indexOf('T');
  if (t > 0 && iso.length() >= t + 6)
    return iso.substring(t + 1, t + 3) + ":" + iso.substring(t + 4, t + 6);

  return "--:--";
}

static String compactDuration(const String& dur) {

  int d_sep = dur.indexOf('d');
  int days = 0;
  int off = 0;

  if (d_sep > 0) {
    days = dur.substring(0, d_sep).toInt();
    off = d_sep + 1;
  }

  if (dur.length() < off + 7) return dur;

  int h = dur.substring(off, off + 2).toInt();
  int m = dur.substring(off + 3, off + 5).toInt();

  h += days * 24;
  if (h <= 0) return String(m) + "m";

  char buf[16];
  snprintf(buf, sizeof(buf), "%dh%02dm", h, m);
  return String(buf);
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

// =========================== Splash Screen ===========================
static void showSplash(uint16_t ms = 2000) {
  gfx->fillScreen(RGB565_BLACK);

  // Disegno immagine 480x480
  // (se l'immagine è esattamente 480x480)
  gfx->draw16bitRGBBitmap(
    0, 0,
    (uint16_t*)SquaredCoso,  // cast obbligatorio
    480, 480);

  delay(ms);
}


// =========================== Sanificazione testo (UTF8 → ASCII sicuro) ============================
static String sanitizeText(const String& in) {
  String out;
  out.reserve(in.length());

  for (int i = 0; i < in.length();) {
    uint8_t c = (uint8_t)in[i];

    // ASCII semplice
    if (c < 0x80) {
      out += (char)c;
      i++;
      continue;
    }

    // UTF-8 2 byte — accenti italiani principali
    if (c == 0xC3 && i + 1 < in.length()) {
      uint8_t c2 = (uint8_t)in[i + 1];

      switch (c2) {
        case 0xA0: out += 'a'; break;  // à
        case 0xA1: out += 'a'; break;  // á
        case 0xA8: out += 'e'; break;  // è
        case 0xA9: out += 'e'; break;  // é
        case 0xAC: out += 'i'; break;  // ì
        case 0xAD: out += 'i'; break;  // í
        case 0xB2: out += 'o'; break;  // ò
        case 0xB3: out += 'o'; break;  // ó
        case 0xB9: out += 'u'; break;  // ù
        case 0xBA: out += 'u'; break;  // ú
        default:
          out += '?';
      }

      i += 2;
      continue;
    }

    // UTF-8 3 byte: “ ” … — varie punte e virgolette
    if (c == 0xE2 && i + 2 < in.length()) {
      uint8_t c2 = (uint8_t)in[i + 1];
      uint8_t c3 = (uint8_t)in[i + 2];

      // virgolette curve
      if (c2 == 0x80 && (c3 == 0x9C || c3 == 0x9D)) {
        out += '\'';
        i += 3;
        continue;
      }

      // virgolette basse « »
      if (c2 == 0x80 && (c3 == 0xB9 || c3 == 0xBA)) {
        out += '\'';
        i += 3;
        continue;
      }

      // trattini lunghi, en dash, em dash → '-'
      if (c2 == 0x80 && (c3 == 0x93 || c3 == 0x94)) {
        out += '-';
        i += 3;
        continue;
      }

      // puntini sospensione
      if (c2 == 0x80 && c3 == 0xA6) {
        out += "...";
        i += 3;
        continue;
      }

      // bullet •
      if (c2 == 0x80 && c3 == 0xA2) {
        out += '*';
        i += 3;
        continue;
      }

      out += '?';
      i += 3;
      continue;
    }

    // fallback su carattere sconosciuto
    out += '?';
    i++;
  }

  // comprimi doppi spazi
  while (out.indexOf("  ") >= 0) out.replace("  ", " ");

  out.trim();
  return out;
}

// Decodifica \u00XX → carattere UTF-8 reale
static String decodeJsonUnicode(const String& s) {
  String out;
  out.reserve(s.length());
  for (int i = 0; i < s.length();) {

    // match "\u00XX"
    if (i + 6 <= s.length() && s[i] == '\\' && s[i + 1] == 'u' && s[i + 2] == '0' && s[i + 3] == '0') {

      char h1 = s[i + 4];
      char h2 = s[i + 5];

      int high = (h1 <= '9' ? h1 - '0' : (tolower(h1) - 'a' + 10));
      int low = (h2 <= '9' ? h2 - '0' : (tolower(h2) - 'a' + 10));
      int val = (high << 4) | low;

      // converte in UTF-8
      char buf[3];
      if (val < 0x80) {
        buf[0] = (char)val;
        buf[1] = 0;
        out += buf;
      } else {
        buf[0] = 0xC0 | (val >> 6);
        buf[1] = 0x80 | (val & 0x3F);
        buf[2] = 0;
        out += buf;
      }

      i += 6;
      continue;
    }

    out += s[i];
    i++;
  }
  return out;
}



static String clip15(const String& s) {
  String t = sanitizeText(s);
  if (t.length() <= 15) return t;
  return t.substring(0, 15) + "...";
}

// JSON escape minimale per body OpenAI
static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);
  for (int i = 0; i < (int)s.length(); ++i) {
    char c = s.charAt(i);
    if (c == '\\') out += "\\\\";
    else if (c == '\"') out += "\\\"";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }
  return out;
}

// =========================== UI: Testo ======================================
static void drawBoldTextColored(int16_t x, int16_t y, const String& raw, uint16_t fg, uint16_t bg) {
  String s = sanitizeText(raw);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x + 1, y);
  gfx->print(s);
  gfx->setCursor(x, y + 1);
  gfx->print(s);
  gfx->setCursor(x + 1, y + 1);
  gfx->print(s);
  gfx->setCursor(x, y);
  gfx->print(s);
}
static void drawBoldMain(int16_t x, int16_t y, const String& raw) {
  drawBoldTextColored(x, y, raw, COL_TEXT, COL_BG);
}

static void drawCenteredBold(int16_t y, const String& raw, uint8_t scale, uint16_t fg, uint16_t bg) {
  String s = sanitizeText(raw);
  int w = s.length() * BASE_CHAR_W * scale;
  int x = (480 - w) / 2;
  gfx->setTextSize(scale);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x + 1, y);
  gfx->print(s);
  gfx->setCursor(x, y + 1);
  gfx->print(s);
  gfx->setCursor(x + 1, y + 1);
  gfx->print(s);
  gfx->setCursor(x, y);
  gfx->print(s);
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
static void drawHLine(int y) {
  gfx->drawLine(PAGE_X, y, PAGE_X + PAGE_W, y, COL_DIVIDER);
}

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
static String g_ics = "";
static String g_lat = "";
static String g_lon = "";
static uint32_t PAGE_INTERVAL_MS = 15000;

static String g_from_station = "Bellinzona";
static String g_to_station = "Lugano";

struct CDEvent {
  String name;
  String whenISO;
};
static CDEvent cd[8];

// Nuove impostazioni QOD/OpenAI
static String g_oa_key = "";    // chiave API OpenAI
static String g_oa_topic = "";  // argomento frase (es. "pazienza", "motivazione")

// ======= PAGINE / ROTAZIONE =================================================
enum Page {
  P_WEATHER = 0,
  P_AIR = 1,
  P_CLOCK = 2,
  P_CAL = 3,
  P_BTC = 4,
  P_QOD = 5,
  P_INFO = 6,
  P_COUNT = 7,
  P_FX = 8,    // Valute CHF Fast
  P_T24 = 9,   // Mini grafico temperatura 24h
  P_SUN = 10,  // Ore di luce del giorno
  PAGES = 11
};


static int g_page = 0;

// Array di visibilità pagina (persistente via bitmask).
static bool g_show[PAGES] = {
  true,  // WEATHER
  true,  // AIR
  true,  // CLOCK
  true,  // CAL
  true,  // BTC
  true,  // QOD
  true,  // INFO
  true,  // COUNT
  true,  // FX
  true,  // T24
  true   // SUN
};

// Helpers mask <-> array
static uint16_t pagesMaskFromArray() {
  uint16_t m = 0;
  for (int i = 0; i < PAGES; i++)
    if (g_show[i]) m |= (1u << i);
  return m;
}
static void pagesArrayFromMask(uint16_t m) {
  for (int i = 0; i < PAGES; i++) g_show[i] = (m & (1u << i)) != 0;
  // Safety: se tutte false, abilita almeno l'orologio
  bool any = false;
  for (int i = 0; i < PAGES; i++)
    if (g_show[i]) {
      any = true;
      break;
    }
  if (!any) {
    for (int i = 0; i < PAGES; i++) g_show[i] = false;
    g_show[P_CLOCK] = true;
  }
}

// Trova la prima pagina abilitata; -1 se nessuna.
static int firstEnabledPage() {
  for (int i = 0; i < PAGES; i++)
    if (g_show[i]) return i;
  return -1;
}
// Avanza g_page alla prossima pagina abilitata (wrap); ritorna false se nessuna.
static bool advanceToNextEnabled() {
  if (firstEnabledPage() < 0) return false;
  for (int k = 0; k < PAGES; k++) {
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
  else {  // impossibile: fallback
    g_page = P_CLOCK;
    for (int i = 0; i < PAGES; i++) g_show[i] = false;
    g_show[P_CLOCK] = true;
  }
}

// =========================== HTTP helpers ===================================
static inline bool isHttpOk(int code) {
  return code >= 200 && code < 300;
}
static bool httpGET(const String& url, String& out, uint32_t timeout = 8000) {
  HTTPClient http;
  http.setTimeout(timeout);
  if (!http.begin(url)) return false;
  int code = http.GET();
  if (!isHttpOk(code)) {
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  return true;
}
static int indexOfCI(const String& s, const String& pat, int from = 0) {
  String S = s;
  S.toLowerCase();
  String P = pat;
  P.toLowerCase();
  return S.indexOf(P, from);
}




// =========================== Bitcoin CHF ====================================


// =========================== Frase del giorno ================================


static String formatBytes(size_t b) {
  if (b >= (1 << 20)) return String(b / (1 << 20)) + " MB";
  if (b >= (1 << 10)) return String(b / (1 << 10)) + " KB";
  return String(b) + " B";
}
static String macStr() {
  uint8_t m[6];
  WiFi.macAddress(m);
  char buf[24];
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(buf);
}
static String formatShortDate(time_t t) {
  struct tm lt;
  localtime_r(&t, &lt);
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d/%02d", lt.tm_mday, lt.tm_mon + 1);
  return String(buf);
}
static String formatUptime() {
  unsigned long ms = millis();
  unsigned long sec = ms / 1000UL;
  unsigned int d = sec / 86400UL;
  sec %= 86400UL;
  unsigned int h = sec / 3600UL;
  sec %= 3600UL;
  unsigned int m = sec / 60UL;
  sec %= 60UL;
  char buf[48];
  snprintf(buf, sizeof(buf), "%ud %02u:%02u:%02lu", d, h, m, sec);
  return String(buf);
}
static void pageInfo() {
  drawHeader("Info dispositivo");
  int y = PAGE_Y;

  bool sta = (WiFi.getMode() == WIFI_STA && WiFi.status() == WL_CONNECTED);
  String mode = (WiFi.getMode() == WIFI_AP) ? "AP" : (sta ? "STA" : "Idle");

  String ip = sta ? WiFi.localIP().toString() : (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : "n/d");
  String gw = sta ? WiFi.gatewayIP().toString() : "n/d";
  String ssid = sta ? WiFi.SSID() : (WiFi.getMode() == WIFI_AP ? ap_ssid : "n/d");
  int chan = sta ? WiFi.channel() : 0;

  drawBoldMain(PAGE_X, y + CHAR_H, String("Mode: ") + mode);
  y += CHAR_H * 2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("SSID: ") + sanitizeText(ssid));
  y += CHAR_H * 2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("IP  : ") + ip + String("   GW: ") + gw);
  y += CHAR_H * 2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("MAC : ") + macStr() + String("   CH: ") + String(chan));
  y += CHAR_H * 2 + 4;

  drawHLine(y);
  y += 10;

  drawBoldMain(PAGE_X, y + CHAR_H, String("Uptime   : ") + formatUptime());
  y += CHAR_H * 2 + 4;

  size_t ps = ESP.getPsramSize();
  if (ps > 0) {
    drawBoldMain(PAGE_X, y + CHAR_H, String("PSRAM    : ") + formatBytes(ps));
    y += CHAR_H * 2 + 4;
  }

  drawBoldMain(PAGE_X, y + CHAR_H, String("Flash    : ") + formatBytes(ESP.getFlashChipSize()));
  y += CHAR_H * 2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, String("Sketch   : ") + formatBytes(ESP.getSketchSize()) + " / " + formatBytes(ESP.getFreeSketchSpace()));
  y += CHAR_H * 2 + 4;

  drawBoldMain(PAGE_X, y + CHAR_H, String("CPU      : ") + String(ESP.getCpuFreqMHz()) + " MHz");
  y += CHAR_H * 2 + 4;
}



// =========================== Dati / Refresh =================================
static uint32_t lastRefresh = 0;
static const uint32_t REFRESH_MS = 10UL * 60UL * 1000UL;
static uint32_t lastPageSwitch = 0;
volatile bool g_dataRefreshPending = false;

static void drawCurrentPage() {
  ensureCurrentPageEnabled();
  gfx->fillScreen(COL_BG);
  switch (g_page) {
    case P_WEATHER:
      if (g_show[P_WEATHER]) pageWeather();
      else { /*no-op*/ }
      break;
    case P_AIR:
      if (g_show[P_AIR]) pageAir();
      break;
    case P_CLOCK:
      if (g_show[P_CLOCK]) pageClock();
      break;
    case P_CAL:
      if (g_show[P_CAL]) pageCalendar();
      break;
    case P_BTC:
      if (g_show[P_BTC]) pageBTC();
      break;
    case P_QOD:
      if (g_show[P_QOD]) pageQOD();
      break;
    case P_INFO:
      if (g_show[P_INFO]) pageInfo();
      break;
    case P_COUNT:
      if (g_show[P_COUNT]) pageCountdowns();
      break;
    case P_FX:
      if (g_show[P_FX]) pageFX();
      break;

    case P_T24:
      if (g_show[P_T24]) pageTemp24();
      break;

    case P_SUN:
      if (g_show[P_SUN]) pageSun();
      break;

      //case P_PT:      if (g_show[P_PT])      pagePT();      break;
  }
}
static void refreshAll() {
  if (g_show[P_WEATHER]) fetchWeather();
  if (g_show[P_AIR])     fetchAir();
  if (g_show[P_CAL])     fetchICS();
  if (g_show[P_BTC])     fetchBTC();
  if (g_show[P_QOD])     fetchQOD();
  if (g_show[P_WEATHER]) fetchWeather();

  // NUOVE PAGINE
  if (g_show[P_FX])      fetchFX();      // Valute CHF Fast
  if (g_show[P_T24])     fetchTemp24();  // Grafico temperatura 24h
  if (g_show[P_SUN])     fetchSun();     // Ore di luce

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
    "<p>Se il popup non compare, apri <b>http://"
    + ip + "/</b></p>"
           "</body></html>";
  return page;
}

// ------- HTML SETTINGS (aggiunta sezione Pagine visibili + OpenAI QOD) -----
static String checkbox(const char* name, bool checked, const char* label) {
  return String("<label style='display:flex;gap:8px;align-items:center'><input type='checkbox' name='") + name + "' value='1' " + (checked ? "checked" : "") + "/><span>" + label + "</span></label>";
}

static String htmlSettings(bool saved = false, const String& msg = "") {
  String notice = saved ? "<div class='ok'>Impostazioni salvate. Aggiornamento in corso…</div>" : (msg.length() ? ("<div class='warn'>" + msg + "</div>") : "");
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
    + notice + "<div class='card'>"
               "<form method='POST' action='/settings'>"
               "<div class='row'>"
               "<div><label>Citta</label><input name='city' value='"
    + sanitizeText(g_city) + "' placeholder='Bellinzona'/></div>"
                             "<div><label>Lingua (it/en)</label><input name='lang' value='"
    + sanitizeText(g_lang) + "' placeholder='it'/></div>"
                             "</div>"
                             "<label>URL Calendario ICS (opzionale)</label>"
                             "<input name='ics' value='"
    + sanitizeText(g_ics) + "' placeholder='https://.../calendar.ics'/>"
                            "<div class='row'>"
                            "<div><label>Tempo cambio pagina (secondi)</label><input name='page_s' type='number' min='5' max='600' value='"
    + String(page_s) + "'/></div>"
                       "</div>"
                       "</div>"
                       "<br>"
                       // --- QOD + BITCOIN (corretto, dentro il form, senza rotture di layout) ---
                       "<div class='card'>"
                       "<h3>Quote of the day</h3>"
                       "<p style='font-size:.9rem;opacity:.8'>Se imposti una chiave OpenAI verra usato GPT per generare la frase. "
                       "Se lasci vuoto, resta il fallback ZenQuotes.</p>"

                       "<label>Chiave API OpenAI</label>"
                       "<input name='openai_key' type='password' value='"
    + sanitizeText(g_oa_key) + "' placeholder='sk-...'/>"

                               "<label>Argomento frase</label>"
                               "<input name='openai_topic' value='"
    + sanitizeText(g_oa_topic) + "' placeholder='es. motivazione'/>"
                                 "</div>"
                                 "<br>"

                                 "<div class='card'>"
                                 "<h3>Bitcoin</h3>"
                                 "<label>Quantita BTC posseduti (opzionale)</label>"
                                 "<input name='btc_owned' type='number' step='0.00000001' value='"
    + (isnan(g_btc_owned) ? String("") : String(g_btc_owned, 8)) + "'/>"
                                                                   "</div>"
                                                                   "<br>"

                                                                   "<div class='card'>"
                                                                   "<h3>Pagine visibili</h3>"
                                                                   "<div class='grid'>"
    + checkbox("p_WEATHER", g_show[P_WEATHER], "Meteo (wttr.in)") + checkbox("p_AIR", g_show[P_AIR], "Qualità aria (Open-Meteo)") + checkbox("p_CLOCK", g_show[P_CLOCK], "Orologio") + checkbox("p_CAL", g_show[P_CAL], "Calendario ICS (oggi)") + checkbox("p_BTC", g_show[P_BTC], "Bitcoin in CHF") + checkbox("p_QOD", g_show[P_QOD], "Frase del giorno") + checkbox("p_INFO", g_show[P_INFO], "Info dispositivo") + checkbox("p_COUNT", g_show[P_COUNT], "Countdown") + checkbox("p_FX", g_show[P_FX], "Valute (CHF Fast)") + checkbox("p_T24", g_show[P_T24], "Temperatura 24h (grafico)") + checkbox("p_SUN", g_show[P_SUN], "Ore di luce") +
    "</div>"
    "<p style='opacity:.8;margin-top:8px'>Se le deselezioni tutte, l'Orologio resterà comunque attivo.</p>"
    "</div>"
    "<br>"
    "<div class='card'>"
    "<h3>Countdown eventi (max 8)</h3>"
    "<div class='row'>"
    "<div><label>Nome #1</label><input name='cd1n' value='"
    + sanitizeText(cd[0].name) + "' placeholder='Evento 1'/></div>"
                                 "<div><label>Data/Ora #1</label><input name='cd1t' type='datetime-local' value='"
    + sanitizeText(cd[0].whenISO) + "'/></div>"
                                    "</div>"
                                    "<div class='row'>"
                                    "<div><label>Nome #2</label><input name='cd2n' value='"
    + sanitizeText(cd[1].name) + "' placeholder='Evento 2'/></div>"
                                 "<div><label>Data/Ora #2</label><input name='cd2t' type='datetime-local' value='"
    + sanitizeText(cd[1].whenISO) + "'/></div>"
                                    "</div>"
                                    "<div class='row'>"
                                    "<div><label>Nome #3</label><input name='cd3n' value='"
    + sanitizeText(cd[2].name) + "' placeholder='Evento 3'/></div>"
                                 "<div><label>Data/Ora #3</label><input name='cd3t' type='datetime-local' value='"
    + sanitizeText(cd[2].whenISO) + "'/></div>"
                                    "</div>"
                                    "<div class='row'>"
                                    "<div><label>Nome #4</label><input name='cd4n' value='"
    + sanitizeText(cd[3].name) + "' placeholder='Evento 4'/></div>"
                                 "<div><label>Data/Ora #4</label><input name='cd4t' type='datetime-local' value='"
    + sanitizeText(cd[3].whenISO) + "'/></div>"
                                    "</div>"
                                    "<div class='row'>"
                                    "<div><label>Nome #5</label><input name='cd5n' value='"
    + sanitizeText(cd[4].name) + "' placeholder='Evento 5'/></div>"
                                 "<div><label>Data/Ora #5</label><input name='cd5t' type='datetime-local' value='"
    + sanitizeText(cd[4].whenISO) + "'/></div>"
                                    "</div>"
                                    "<div class='row'>"
                                    "<div><label>Nome #6</label><input name='cd6n' value='"
    + sanitizeText(cd[5].name) + "' placeholder='Evento 6'/></div>"
                                 "<div><label>Data/Ora #6</label><input name='cd6t' type='datetime-local' value='"
    + sanitizeText(cd[5].whenISO) + "'/></div>"
                                    "</div>"
                                    "<div class='row'>"
                                    "<div><label>Nome #7</label><input name='cd7n' value='"
    + sanitizeText(cd[6].name) + "' placeholder='Evento 7'/></div>"
                                 "<div><label>Data/Ora #7</label><input name='cd7t' type='datetime-local' value='"
    + sanitizeText(cd[6].whenISO) + "'/></div>"
                                    "</div>"
                                    "<div class='row'>"
                                    "<div><label>Nome #8</label><input name='cd8n' value='"
    + sanitizeText(cd[7].name) + "' placeholder='Evento 8'/></div>"
                                 "<div><label>Data/Ora #8</label><input name='cd8t' type='datetime-local' value='"
    + sanitizeText(cd[7].whenISO) + "'/></div>"
                                    "</div>"
                                    "<p style='margin-top:14px'>"
                                    "<button class='btn ghost' formaction='/force_qod' formmethod='POST'>Richiedi nuova frase</button>"
                                    "</p>"
                                    "<p style='margin-top:14px'><button class='btn primary' type='submit'>Salva</button> "
                                    "<a class='btn ghost' href='/'>Home</a></p>"
                                    "</form>    <!-- chiudi la form di /settings PRIMA del blocco export/import -->"

                                    "<p><a class='btn ghost' href='/export'>Esporta impostazioni</a></p>"
                                    "<div class='card'>"
                                    "<h3>Importa impostazioni</h3>"
                                    "<form method='POST' action='/import' enctype='multipart/form-data'>"
                                    "<input type='file' name='file' accept='application/json'/>"
                                    "<p style='margin-top:10px'><button class='btn primary' type='submit'>Carica</button></p>"
                                    "</form>"
                                    "</div>"


                                    "</form>"
                                    "</div>"
                                    "</main></body></html>";
  return page;
}

// -------------------- Handlers AP/STA ---------------------------------------
static void handleRootAP() {
  web.send(200, "text/html; charset=utf-8", htmlAP());
}
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
static void handleReboot() {
  web.send(200, "text/plain; charset=utf-8", "OK");
  delay(100);
  ESP.restart();
}

static String htmlHome() {
  uint32_t s = PAGE_INTERVAL_MS / 1000;
  String enabledList = "";
  const char* names[PAGES] = { "Meteo", "Aria", "Orologio", "Calendario", "BTC", "Frase", "Info", "Countdown" };
  //,"Partenza"};
  for (int i = 0; i < PAGES; i++) {
    if (g_show[i]) {
      if (enabledList.length()) enabledList += ", ";
      enabledList += names[i];
    }
  }
  String page = "<!doctype html><html><head><meta charset='utf-8'/>"
                "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
                "<title>Gat Multi Ticker</title>"
                "<style>body{font-family:system-ui,Segoe UI,Roboto,Ubuntu,Arial,sans-serif}</style>"
                "</head><body><h2>Gat Multi Ticker</h2>";
  page += String("<p><b>Citta:</b> ") + sanitizeText(g_city) + String("<br><b>Lingua:</b> ") + sanitizeText(g_lang) + String("<br><b>Intervallo cambio pagina:</b> ") + String(s) + String(" s</p>");
  page += String("<p><b>Pagine attive:</b> ") + enabledList + "</p>";
  page += String("<p><b>Collegamento:</b> ") + sanitizeText(g_from_station) + " → " + sanitizeText(g_to_station) + "</p>";
  if (g_oa_key.length()) {
    page += String("<p><b>Quote of the day:</b> OpenAI (tema: ") + sanitizeText(g_oa_topic) + ")</p>";
  } else {
    page += "<p><b>Quote of the day:</b> ZenQuotes (default)</p>";
  }
  page += "<p><a href='/settings'>Impostazioni</a></p></body></html>";
  return page;
}
static void handleRootSTA() {
  web.send(200, "text/html; charset=utf-8", htmlHome());
}

static void handleExport() {
  // Costruisci il JSON delle impostazioni
  String json = "{";
  json += "\"city\":\"" + jsonEscape(g_city) + "\",";
  json += "\"lang\":\"" + jsonEscape(g_lang) + "\",";
  json += "\"ics\":\"" + jsonEscape(g_ics) + "\",";
  json += "\"lat\":\"" + jsonEscape(g_lat) + "\",";
  json += "\"lon\":\"" + jsonEscape(g_lon) + "\",";
  json += "\"page_ms\":" + String(PAGE_INTERVAL_MS) + ",";
  json += "\"oa_key\":\"" + jsonEscape(g_oa_key) + "\",";
  json += "\"oa_topic\":\"" + jsonEscape(g_oa_topic) + "\",";
  json += "\"pages_mask\":" + String(pagesMaskFromArray()) + ",";
  json += "\"countdowns\":[";
  for (int i = 0; i < 8; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"" + jsonEscape(cd[i].name) + "\","
                                                      "\"when\":\""
            + jsonEscape(cd[i].whenISO) + "\"}";
  }
  json += "]}";

  // Forza lunghezza nota OBBLIGATORIA
  web.setContentLength(json.length());

  // QUesti 3 header *DEVONO* venire prima della sendContent
  web.sendHeader("Content-Disposition", "attachment; filename=\"gat_config.json\"");
  web.sendHeader("Content-Type", "application/octet-stream");
  web.sendHeader("Connection", "close");

  // NON usare web.send()
  // Crea header HTTP manuale
  web.send(200);

  // Invia il contenuto RAW
  web.sendContent(json);

  // Chiudi connessione
  web.client().stop();
}



static void handleImport() {
  if (!web.hasArg("file") || web.arg("file").length() == 0) {
    web.send(400, "text/plain", "Nessun file caricato");
    return;
  }

  String body = web.arg("file");

  auto getStr = [&](const char* key) -> String {
    String k = String("\"") + key + "\":\"";
    int p = body.indexOf(k);
    if (p < 0) return "";
    int s = p + k.length();
    int e = body.indexOf("\"", s);
    if (e < 0) return "";
    return body.substring(s, e);
  };
  auto getNum = [&](const char* key) -> long {
    String k = String("\"") + key + "\":";
    int p = body.indexOf(k);
    if (p < 0) return -1;
    int s = p + k.length();
    int e = s;
    while (e < (int)body.length() && (isdigit(body[e]) || body[e] == '.')) e++;
    return body.substring(s, e).toInt();
  };

  g_city = getStr("city");
  g_lang = getStr("lang");
  g_ics = getStr("ics");
  g_lat = getStr("lat");
  g_lon = getStr("lon");
  long ms = getNum("page_ms");
  if (ms > 0) PAGE_INTERVAL_MS = ms;
  g_from_station = getStr("from");
  g_to_station = getStr("to");
  g_oa_key = getStr("openai_key");
  g_oa_topic = getStr("openai_topic");

  uint16_t mask = (uint16_t)getNum("pages_mask");
  pagesArrayFromMask(mask);

  for (int i = 0; i < 8; i++) {
    String tagN = "\"name\":\"";
    String tagT = "\"time\":\"";
    int block = body.indexOf("{", body.indexOf("\"cd\""));
    for (int k = 0; k < i; k++) block = body.indexOf("{", block + 1);
    if (block < 0) break;
    int bn = body.indexOf(tagN, block);
    int bt = body.indexOf(tagT, block);
    if (bn < 0 || bt < 0) break;

    int s1 = bn + tagN.length();
    int e1 = body.indexOf("\"", s1);
    int s2 = bt + tagT.length();
    int e2 = body.indexOf("\"", s2);

    cd[i].name = (e1 > s1 ? body.substring(s1, e1) : "");
    cd[i].whenISO = (e2 > s2 ? body.substring(s2, e2) : "");
  }

  //saveAppConfig();
  g_dataRefreshPending = true;

  web.send(200, "text/html; charset=utf-8",
           "<!doctype html><meta charset='utf-8'><body>"
           "<h3>Impostazioni importate.</h3>"
           "<p><a href='/settings'>Torna alle impostazioni</a></p>"
           "</body>");
}

static void startDNSCaptive() {
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}
static void startAPPortal() {
  web.on("/", HTTP_GET, handleRootAP);
  web.on("/save", HTTP_POST, handleSave);
  web.on("/reboot", HTTP_GET, handleReboot);
  web.on("/export", HTTP_GET, handleExport);

  web.onNotFound(handleRootAP);
  web.begin();
}
static void startSTAWeb() {
  web.on("/", HTTP_GET, handleRootSTA);
  web.on("/settings", HTTP_ANY, handleSettings);
  web.on("/force_qod", HTTP_POST, handleForceQOD);
  web.on("/import", HTTP_POST, handleImport);

  web.onNotFound(handleRootSTA);
  web.begin();
}

// =========================== Wi-Fi ==========================================
static void drawAPScreenOnce(const String& ssid, const String& pass) {
  gfx->fillScreen(COL_BG);
  drawBoldTextColored(16, 36, "Connettiti all'AP:", COL_TEXT, COL_BG);
  drawBoldTextColored(16, 66, ssid, COL_TEXT, COL_BG);
  gfx->setTextSize(1);
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(16, 96);
  gfx->print("Password: ");
  gfx->print(pass);
  gfx->setCursor(16, 114);
  gfx->print("Captive portal automatico.");
  gfx->setCursor(16, 126);
  gfx->print("Se non compare, apri l'IP dell'AP.");
  gfx->setTextSize(TEXT_SCALE);
}
static void startAPWithPortal() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char ssidbuf[32];
  snprintf(ssidbuf, sizeof(ssidbuf), "PANEL-%02X%02X", mac[4], mac[5]);
  ap_ssid = ssidbuf;
  ap_pass = "panelsetup";
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str());
  delay(100);
  startDNSCaptive();
  startAPPortal();
  drawAPScreenOnce(ap_ssid, ap_pass);
}
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
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(100);
    yield();
  }
  return WiFi.status() == WL_CONNECTED;
}



// =========================== Setup / Loop ===================================
void setup() {
  Serial.begin(115200);
  backlightOn();
  panelKickstart();

  // --- Splash screen al posto della scritta "Avvio..." ---
  showSplash(2000);  // 2 secondi (regolabile)

  // Dopo lo splash NON mostra nulla: il resto del setup prosegue normalmente



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
    dnsServer.processNextRequest();
    web.handleClient();
    delay(10);
    return;
  }

  web.handleClient();

  if (WiFi.status() != WL_CONNECTED) {
    static uint32_t nextTry = 0;
    if (millis() > nextTry) {
      nextTry = millis() + 5000;
      if (tryConnectSTA(5000)) {
        if (!g_timeSynced) syncTimeFromNTP();
        g_dataRefreshPending = true;
      }
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
