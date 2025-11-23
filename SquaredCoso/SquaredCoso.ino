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

int indexOfCI(const String& src, const String& key, int from = 0);

// file esterni
#include "images/SquaredCoso.h"
#include "handlers/globals.h"
#include "handlers/settingshandler.h"

#include "pages/SquaredCount.h"
#include "pages/SquaredCal.h"
#include "pages/SquaredNews.h"
#include "pages/SquaredClock.h"
#include "pages/SquaredCrypto.h"
#include "pages/SquaredLight.h"
#include "pages/SquaredMeteo.h"
#include "pages/SquaredCHF.h"

#include "images/cosino.h"
#include "images/cosino2.h"
#include "images/cosino6.h"

String g_rss_url = "https://feeds.bbci.co.uk/news/rss.xml";  // default

String g_city = "Bellinzona";
String g_lang = "it";
String g_ics = "";
String g_lat = "";
String g_lon = "";
uint32_t PAGE_INTERVAL_MS = 15000;

String g_from_station = "Bellinzona";
String g_to_station = "Lugano";

CDEvent cd[8];

String g_oa_key = "";
String g_oa_topic = "";

int g_page = 0;
bool g_cycleCompleted = false;

bool g_show[PAGES] = {
  true, true, true, true, true,
  true, true, true, true, true, true, true
};

double g_btc_owned = NAN;
String g_fiat = "CHF";

volatile bool g_dataRefreshPending = false;


// =========================== Display / PWM ==================================
#define GFX_BL 38
#define PWM_CHANNEL 0
#define PWM_FREQ 1000
#define PWM_BITS 8


bool geocodeIfNeeded() {

  // Se già esistono lat/lon → ok
  if (g_lat.length() && g_lon.length())
    return true;

  String url =
    "https://geocoding-api.open-meteo.com/v1/search?count=1&format=json"
    "&name="
    + g_city + "&language=" + g_lang;

  String body;
  if (!httpGET(url, body, 10000))
    return false;

  // latitude
  int p = indexOfCI(body, "\"latitude\"", 0);
  if (p < 0) return false;
  int c = body.indexOf(':', p);
  int e = body.indexOf(',', c + 1);
  g_lat = sanitizeText(body.substring(c + 1, e));

  // longitude
  p = indexOfCI(body, "\"longitude\"", 0);
  if (p < 0) return false;
  c = body.indexOf(':', p);
  e = body.indexOf(',', c + 1);
  g_lon = sanitizeText(body.substring(c + 1, e));

  if (!g_lat.length() || !g_lon.length())
    return false;

  saveAppConfig();
  return true;
}


static void quickFadeOut() {
  const int maxDuty = (1 << PWM_BITS) - 1;
  const int steps = 50;     // fade rapido 50 step
  const int stepDelay = 2;  // ~100ms totali

  for (int i = steps; i >= 0; i--) {
    int duty = (maxDuty * i) / steps;
    ledcWrite(PWM_CHANNEL, duty);
    delay(stepDelay);
  }
}

static void quickFadeIn() {
  const int maxDuty = (1 << PWM_BITS) - 1;
  const int steps = 50;
  const int stepDelay = 2;

  for (int i = 0; i <= steps; i++) {
    int duty = (maxDuty * i) / steps;
    ledcWrite(PWM_CHANNEL, duty);
    delay(stepDelay);
  }
}


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
const uint16_t COL_BG = 0x1B70;
const uint16_t COL_HEADER = 0x2967;
const uint16_t COL_TEXT = 0xFFFF;
const uint16_t COL_SUBTEXT = 0xC618;
const uint16_t COL_DIVIDER = 0xFFE0;
const uint16_t COL_ACCENT1 = 0xFFFF;
const uint16_t COL_ACCENT2 = 0x07FF;
const uint16_t COL_GOOD = 0x07E0;
const uint16_t COL_WARN = 0xFFE0;
const uint16_t COL_BAD = 0xF800;
uint16_t g_air_bg = COL_BG;

// =========================== Layout =========================================
static const int HEADER_H = 50;
const int PAGE_X = 16;
const int PAGE_Y = HEADER_H + 12;
static const int PAGE_W = 480 - 32;
static const int PAGE_H = 480 - PAGE_Y - 16;
const int BASE_CHAR_W = 6;
const int BASE_CHAR_H = 8;
const int TEXT_SCALE = 2;
static const int CHAR_W = BASE_CHAR_W * TEXT_SCALE;
const int CHAR_H = BASE_CHAR_H * TEXT_SCALE;
static const int ITEMS_LINES_SP = 6;

// pagine
// === PAGINE ===
#include "pages/SquaredMeteo.h"
#include "pages/SquaredTemp.h"
#include "pages/SquaredClock.h"
#include "pages/SquaredSay.h"
#include "pages/SquaredInfo.h"
#include "pages/SquaredLight.h"
#include "pages/SquaredCHF.h"
#include "pages/SquaredAir.h"
#include "pages/SquaredCrypto.h"

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
void todayYMD(String& ymd) {
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

Preferences prefs;
DNSServer dnsServer;
WebServer web(80);

String sta_ssid, sta_pass;
String ap_ssid, ap_pass;
const byte DNS_PORT = 53;

static void panelKickstart() {
  delay(50);
  gfx->begin();
  gfx->setRotation(0);
  delay(120);
  gfx->displayOn();
  delay(20);
  //gfx->fillScreen(COL_BG);
}

// =========================== Splash Screen ===========================
static void showSplashFadeInOnly(uint16_t ms = 2000) {

  gfx->fillScreen(RGB565_BLACK);
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t*)SquaredCoso, 480, 480);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);

  const int maxDuty = (1 << PWM_BITS) - 1;
  const int steps = 500;  // fade di 2.5s
  const int stepDelay = 5;

  for (int i = 0; i <= steps; i++) {
    int duty = (maxDuty * i) / steps;
    ledcWrite(PWM_CHANNEL, duty);
    delay(stepDelay);
  }

  delay(ms);  // pausa a luminosità piena
}

static void splashFadeOut() {

  const int maxDuty = (1 << PWM_BITS) - 1;
  const int steps = 250;    // <-- dimezzato
  const int stepDelay = 5;  // 250 * 5ms = ~1.25s

  // Fade-out mentre lo splash è ancora visibile
  for (int i = steps; i >= 0; i--) {
    int duty = (maxDuty * i) / steps;
    ledcWrite(PWM_CHANNEL, duty);
    delay(stepDelay);
  }

  // BL = 0 → schermo buio
  gfx->fillScreen(COL_BG);
}


static void fadeInUI() {
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);

  const int maxDuty = (1 << PWM_BITS) - 1;
  const int steps = 250;  // simmetrico al fade-out
  const int stepDelay = 5;

  ledcWrite(PWM_CHANNEL, 0);

  for (int i = 0; i <= steps; i++) {
    int duty = (maxDuty * i) / steps;
    ledcWrite(PWM_CHANNEL, duty);
    delay(stepDelay);
  }
}

// =========================== Sanificazione testo (UTF8 → ASCII sicuro) ============================
String sanitizeText(const String& in) {
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
// Decodifica sequenze \uXXXX in testo UTF-8
static String decodeJsonUnicode(const String& src) {
  String out;
  out.reserve(src.length());

  for (int i = 0; i < src.length(); i++) {
    char c = src[i];

    if (c == '\\' && i + 5 < src.length() && src[i + 1] == 'u') {
      // Prende XXXX
      String hex = src.substring(i + 2, i + 6);
      int code = (int)strtol(hex.c_str(), nullptr, 16);

      if (code > 0 && code <= 0x10FFFF) {

        // Codifica UTF-8
        if (code < 0x80) {
          out += (char)code;
        } else if (code < 0x800) {
          out += (char)(0xC0 | (code >> 6));
          out += (char)(0x80 | (code & 0x3F));
        } else if (code < 0x10000) {
          out += (char)(0xE0 | (code >> 12));
          out += (char)(0x80 | ((code >> 6) & 0x3F));
          out += (char)(0x80 | (code & 0x3F));
        } else {
          out += (char)(0xF0 | (code >> 18));
          out += (char)(0x80 | ((code >> 12) & 0x3F));
          out += (char)(0x80 | ((code >> 6) & 0x3F));
          out += (char)(0x80 | (code & 0x3F));
        }

        i += 5;  // salta \uXXXX
        continue;
      }
    }

    out += c;
  }

  return out;
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
static void drawBoldTextColored(
  int16_t x, int16_t y,
  const String& raw,
  uint16_t fg, uint16_t bg,
  uint8_t scale = TEXT_SCALE  // <-- questa è l’unica aggiunta
) {

  String s = sanitizeText(raw);

  gfx->setTextSize(scale);  // <-- usa la scala passata
  gfx->setTextColor(fg, bg);

  // quattro offset per effetto "bold"
  gfx->setCursor(x + 1, y);
  gfx->print(s);

  gfx->setCursor(x, y + 1);
  gfx->print(s);

  gfx->setCursor(x + 1, y + 1);
  gfx->print(s);

  gfx->setCursor(x, y);
  gfx->print(s);
}

void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale) {
  drawBoldTextColored(x, y, raw, COL_TEXT, COL_BG, scale);
}

// Overload a 3 parametri (usato da TUTTE le pagine)
void drawBoldMain(int16_t x, int16_t y, const String& raw) {
  drawBoldMain(x, y, raw, TEXT_SCALE);
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

void drawHeader(const String& titleRaw) {
  gfx->fillRect(0, 0, 480, HEADER_H, COL_HEADER);
  String title = sanitizeText(titleRaw);
  drawBoldTextColored(16, 20, title, COL_TEXT, COL_HEADER);
  String dt = getFormattedDateTime();
  if (dt.length()) {
    int tw = dt.length() * CHAR_W;
    drawBoldTextColored(480 - tw - 16, 20, dt, COL_ACCENT1, COL_HEADER);
  }
}
void drawHLine(int y) {
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

// Helpers mask <-> array
uint16_t pagesMaskFromArray() {
  uint16_t m = 0;
  for (int i = 0; i < PAGES; i++)
    if (g_show[i]) m |= (1u << i);
  return m;
}
void pagesArrayFromMask(uint16_t m) {
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
int firstEnabledPage() {
  for (int i = 0; i < PAGES; i++)
    if (g_show[i]) return i;
  return -1;
}
// Avanza g_page alla prossima pagina abilitata (wrap); ritorna false se nessuna.
bool advanceToNextEnabled() {
  if (firstEnabledPage() < 0) return false;
  for (int k = 0; k < PAGES; k++) {
    g_page = (g_page + 1) % PAGES;
    if (g_show[g_page]) return true;
  }
  return false;
}
// Se la pagina corrente è disabilitata, spostati sulla prima abilitata.
void ensureCurrentPageEnabled() {
  if (g_show[g_page]) return;
  int f = firstEnabledPage();
  if (f >= 0) g_page = f;
  else {  // impossibile: fallback
    g_page = P_CLOCK;
    for (int i = 0; i < PAGES; i++) g_show[i] = false;
    g_show[P_CLOCK] = true;
  }
}


// =========================== Dati / Refresh =================================
static uint32_t lastRefresh = 0;
static const uint32_t REFRESH_MS = 10UL * 60UL * 1000UL;
static uint32_t lastPageSwitch = 0;

void pageCountdowns();

static void drawCurrentPage() {
  ensureCurrentPageEnabled();
  gfx->fillScreen(COL_BG);

  switch (g_page) {
    case P_WEATHER:
      pageWeather();
      break;

    case P_AIR:
      pageAir();
      break;

    case P_CLOCK:
      pageClock();
      break;

    case P_CAL:
      pageCalendar();
      break;

    case P_BTC:
      pageCryptoWrapper();
      break;


    case P_QOD:
      pageQOD();
      break;

    case P_INFO:
      pageInfo();
      break;

    case P_COUNT:
      pageCountdowns();
      break;

    case P_FX:
      pageFX();
      break;

    case P_T24:
      pageTemp24();
      break;

    case P_SUN:
      pageSun();
      break;

    case P_NEWS:
      pageNews();
      break;
  }
}



// Flag avvio: evita il disegno UI durante lo splash
static bool g_bootPhase = true;

static void refreshAll() {

  if (g_show[P_WEATHER]) fetchWeather();
  if (g_show[P_AIR]) fetchAir();
  if (g_show[P_CAL]) fetchICS();
  if (g_show[P_BTC]) fetchCryptoWrapper();
  if (g_show[P_QOD]) fetchQOD();
  if (g_show[P_FX]) fetchFX();
  if (g_show[P_T24]) fetchTemp24();
  if (g_show[P_SUN]) fetchSun();
  if (g_show[P_NEWS]) fetchNews();

  // 👉 Durante lo splash NON disegnare l’UI
  if (g_bootPhase) return;

  // 👉 Altrimenti disegna normalmente
  drawCurrentPage();
  lastPageSwitch = millis();
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

bool httpGET(const String& url, String& body, uint32_t timeoutMs = 10000);



// =========================== Setup / Loop ===================================

void setup() {
  panelKickstart();

  // ----------------------------------------------------
  // 1) Splash con F A D E - I N
  // ----------------------------------------------------
  showSplashFadeInOnly(2000);

  // ----------------------------------------------------
  // 2) Carico impostazioni NVS
  // ----------------------------------------------------
  loadAppConfig();

  // ----------------------------------------------------
  // 3) Wi-Fi / NTP
  // ----------------------------------------------------
  if (!tryConnectSTA(8000)) {
    startAPWithPortal();
  } else {
    startSTAWeb();
    syncTimeFromNTP();
    g_dataRefreshPending = true;
  }

  // ----------------------------------------------------
  // 4) Primo refresh dati INVISIBILE (no disegno)
  // ----------------------------------------------------
  refreshAll();

  // ----------------------------------------------------
  // 5) Fade-out dello splash → schermo nero
  // ----------------------------------------------------
  splashFadeOut();

  // ----------------------------------------------------
  // 6) Ora che lo splash è sparito…
  //    permettiamo a refreshAll() di disegnare in futuro
  // ----------------------------------------------------
  g_bootPhase = false;

  // ----------------------------------------------------
  // 7) Disegno UI sotto, ma BL=0 → invisibile
  // ----------------------------------------------------
  drawCurrentPage();

  // ----------------------------------------------------
  // 8) Fade-in morbido della UI reale
  // ----------------------------------------------------
  fadeInUI();
}

// Ritorna un puntatore a una delle 6 immagini Cosino
static const uint16_t* pickRandomCosino() {

  int r = random(0, 3);  // 0..5

  switch (r) {
    case 0: return Cosino;
    case 1: return cosino2;
    case 2: return cosino6;
  }
  return Cosino;  // fallback
}


static void showCycleSplash(uint16_t ms = 1500) {

  const uint16_t* img = pickRandomCosino();

  gfx->fillScreen(RGB565_BLACK);

  // 👉 cast forzato richiesto SOLO da Arduino_GFX
  gfx->draw16bitRGBBitmap(
    0,
    0,
    (uint16_t*)img,  // cast necessario, l’array è const nel .h
    480,
    480);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);

  const int maxDuty = (1 << PWM_BITS) - 1;
  const int steps = 500;
  const int stepDelay = 5;

  // fade-in immagine cosino random
  for (int i = 0; i <= steps; i++) {
    int duty = (maxDuty * i) / steps;
    ledcWrite(PWM_CHANNEL, duty);
    delay(stepDelay);
  }

  delay(ms);
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
    //lastPageSwitch = millis();

    // fade-out veloce
    quickFadeOut();

    // Verifica se stiamo per uscire dall’ultima pagina attiva
    int oldPage = g_page;
    bool ok = advanceToNextEnabled();

    if (!ok || g_page <= oldPage) {
      // ➜ abbiamo completato un giro
      g_cycleCompleted = true;
    }

    if (g_cycleCompleted) {
      // =====================================================
      //  🔁 SPLASH DI TRANSIZIONE A FINE CICLO (COSINO)
      // =====================================================

      // 1) Mostro splash Cosino
      showCycleSplash(1500);

      // 2) Fade-out dello splash → schermo nero
      splashFadeOut();

      // 3) Riparto dalla prima pagina abilitata
      int first = firstEnabledPage();
      if (first < 0) first = P_CLOCK;
      g_page = first;

      g_cycleCompleted = false;

      // 4) Disegno la prima pagina (a BL=0)
      drawCurrentPage();

      // 5) Fade-in
      fadeInUI();
      lastPageSwitch = millis();

      return;
    }

    // cambio pagina normale
    drawCurrentPage();

    // fade-in veloce
    quickFadeIn();
    lastPageSwitch = millis();
  }

  // aggiornamento continuo particelle meteo
  if (g_page == P_WEATHER) {
    pageWeatherParticlesTick();
  }

  // FOGLIE qualità aria
  if (g_page == P_AIR) {
    tickLeaves(g_air_bg);
  }

  // soldi
  if (g_page == P_FX) {
    tickFXDataStream(COL_BG);
  }
  if (g_page == P_SUN) {
    tickSunFX();
  }

  delay(5);
}
