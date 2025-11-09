/*
  Gat News Ticker – ESP32-S3 Panel-4848S040 (RSS Config via Web UI)

  Panoramica:
  - Avvio pannello 480×480 (ST7701 via Arduino_GFX) e retroilluminazione PWM.
  - Wi-Fi: se mancano credenziali → AP con captive portal (solo setup Wi-Fi).
           se connesso in STA → WebUI unica /rss (GET/POST) per gestire feed.
  - Config RSS in NVS (namespace "rss"): url0..url7, limit0..limit7, n.
  - Parser RSS light (string-based) con de-duplicazione e shuffle.
  - Rendering display: top bar blu con testo giallo, separatori verdi, news bianche.
  - Aggiornamento: periodico e immediato dopo salvataggio WebUI (flag).
  - Charsets web: UTF-8 per mostrare correttamente accenti/caratteri speciali.
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

// =========================== Costanti hardware/display ===========================
// Pin retroilluminazione e profilo PWM
#define GFX_BL 38
#define PWM_CHANNEL 0
#define PWM_FREQ 1000
#define PWM_BITS 8

// =========================== NTP/time locale ===========================
// Server NTP e offset per Italia/Svizzera (UTC+1 con DST +1)
static const char* NTP_SERVER = "pool.ntp.org";
static const long GMT_OFFSET_SEC = 3600;
static const int  DAYLIGHT_OFFSET_SEC = 3600;
static bool g_timeSynced = false;

// Flag per richiedere un refresh immediato dei feed dopo salvataggio via WebUI
volatile bool g_rssRefreshPending = false;

// =========================== Bus grafico e display ===========================
// SWSPI per inviare i comandi di init allo ST7701
Arduino_DataBus* bus = new Arduino_SWSPI(
  GFX_NOT_DEFINED, 39, 48, 47, GFX_NOT_DEFINED
);

// Parallelo RGB verso il pannello (timing e pinout del modulo 4848S040)
Arduino_ESP32RGBPanel* rgbpanel = new Arduino_ESP32RGBPanel(
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

// Istanza display 480×480 con sequenza d’inizializzazione ST7701 type9
Arduino_RGB_Display* gfx = new Arduino_RGB_Display(
  480, 480, rgbpanel, 0, true, bus, GFX_NOT_DEFINED,
  st7701_type9_init_operations, sizeof(st7701_type9_init_operations)
);

// =========================== Rete e storage ===========================
Preferences prefs;
DNSServer dnsServer;
WebServer  web(80);

String sta_ssid, sta_pass;
String ap_ssid, ap_pass;
const byte DNS_PORT = 53;

// =========================== Catalogo feed e limiti ===========================
static const char* FEEDS_DEFAULT[4] = {
  "https://www.ansa.it/sito/ansait_rss.xml",
  "https://www.ilsole24ore.com/rss/mondo.xml",
  "https://www.ilsole24ore.com/rss/italia.xml",
  "https://www.fanpage.it/feed/"
};

static const int MAX_ITEMS   = 120;  // buffer globale notizie
static const int MAX_FEEDS   = 8;    // righe configurabili in UI
static const int DEFAULT_LIMIT = 30; // fallback per-feed

struct FeedConfig { String url; int limit; };

// =========================== Buffer contenuti news ===========================
String titles[MAX_ITEMS];
String links [MAX_ITEMS];
int itemCount = 0;

static const uint32_t PAGE_DURATION_MS    = 30000;   // scorrimento pagina
static const uint32_t REFRESH_INTERVAL_MS = 600000;  // refresh feed

uint32_t lastPageSwitch = 0;
uint32_t lastRefresh    = 0;
int currentPage = 0;
static const int ITEMS_PER_PAGE = 4;

// =========================== Palette e layout UI su display ===========================
#define RGB565_BLACK   0x0000
#define RGB565_WHITE   0xFFFF
#define RGB565_BLUE    0x001F   // barra superiore
#define RGB565_YELLOW  0xFFE0   // testo nella barra
#define RGB565_GREEN   0x07E0   // separatori lista

static const int HEADER_H = 56;
static const int PAGE_X   = 16;
static const int PAGE_Y   = HEADER_H + 12;
static const int PAGE_W   = 480 - 32;
static const int PAGE_H   = 480 - PAGE_Y - 16;

static const int ITEM_BOX_H      = PAGE_H / ITEMS_PER_PAGE;
static const int ITEM_MARGIN_X   = 10;
static const int ITEM_MARGIN_TOP = 6;
static const int ITEM_LINE_SP    = 4;

// Metri tipografici (font bitmap base 6×8, scalato ×2)
static const int BASE_CHAR_W = 6;
static const int BASE_CHAR_H = 8;
static const int TEXT_SCALE  = 2;
static const int CHAR_W = BASE_CHAR_W * TEXT_SCALE;
static const int CHAR_H = BASE_CHAR_H * TEXT_SCALE;

// =========================== Pannello: retroilluminazione e avvio ===========================
// Abilita PWM e porta la retroilluminazione a piena intensità
static void backlightOn() {
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(GFX_BL, PWM_CHANNEL);
  ledcWrite(PWM_CHANNEL, 255);
}

// Sequenza base: init controller, accensione display, schermo nero
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

// =========================== Tempo: sync NTP e formattazione ===========================
// Attende un timestamp valido (anno > 2020) entro timeout
static bool waitForValidTime(uint32_t timeoutMs = 8000) {
  uint32_t t0 = millis(); time_t now = 0; struct tm info;
  while ((millis() - t0) < timeoutMs) {
    time(&now); localtime_r(&now, &info);
    if (info.tm_year + 1900 > 2020) return true;
    delay(100);
  }
  return false;
}

// Configura fusi e marca sync; evita doppie richieste NTP
static void syncTimeFromNTP() {
  if (g_timeSynced) return;
  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  g_timeSynced = waitForValidTime(8000);
}

// Converte data/ora in stringa corta “gg/mm - hh:mm”
static String getFormattedDateTime() {
  if (!g_timeSynced) return "";
  time_t now; struct tm t; time(&now); localtime_r(&now, &t);
  char buf[32];
  snprintf(buf, sizeof(buf), "%02d/%02d - %02d:%02d", t.tm_mday, t.tm_mon + 1, t.tm_hour, t.tm_min);
  return String(buf);
}

// =========================== Testo: normalizzazione e disegno ===========================
// Pulisce NBSP, virgolette “strane”, accenti comuni e doppie spaziature
static String normalizeAndTransliterate(const String& in) {
  String s = in;
  s.replace("\xC2\xA0", " ");
  s.replace("à","a"); s.replace("À","A");
  s.replace("è","e"); s.replace("È","E");
  s.replace("é","e"); s.replace("É","E");
  s.replace("ì","i"); s.replace("Ì","I");
  s.replace("ò","o"); s.replace("Ò","O");
  s.replace("ù","u"); s.replace("Ù","U");
  s.replace("\"","'"); s.replace("`","'"); s.replace("\xC2\xB4","'");
  s.replace("\xE2\x80\x98","'"); s.replace("\xE2\x80\x99","'");
  s.replace("\xE2\x80\xBA","'"); s.replace("\xE2\x80\xB9","'");
  s.replace("\xCA\xB9","'");    s.replace("\xE2\x80\xB2","'");
  s.replace("\xCA\xBC","'");    s.replace("\xE2\x80\x9C","'");
  s.replace("\xE2\x80\x9D","'"); s.replace("\xE2\x80\x9E","'");
  s.replace("\xE2\x80\x9F","'"); s.replace("\xCA\xBA","'");
  s.replace("\xE2\x80\xB3","'"); s.replace("\xC2\xAB","'");
  s.replace("\xC2\xBB","'");
  s.trim(); while (s.indexOf("  ") >= 0) s.replace("  ", " ");
  return s;
}

// Disegna titolo con finto “bold” (quattro passate) — testo bianco su fondo nero
static void drawBoldText(int16_t x, int16_t y, const String& raw) {
  String s = normalizeAndTransliterate(raw);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(x+1, y);   gfx->print(s);
  gfx->setCursor(x,   y+1); gfx->print(s);
  gfx->setCursor(x+1, y+1); gfx->print(s);
  gfx->setCursor(x,   y);   gfx->print(s);
}

// Variante con colori personalizzati (usata in header)
static void drawBoldTextColored(int16_t x, int16_t y, const String& raw, uint16_t fg, uint16_t bg) {
  String s = normalizeAndTransliterate(raw);
  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(fg, bg);
  gfx->setCursor(x+1, y);   gfx->print(s);
  gfx->setCursor(x,   y+1); gfx->print(s);
  gfx->setCursor(x+1, y+1); gfx->print(s);
  gfx->setCursor(x,   y);   gfx->print(s);
}

// Stima rapida larghezza in pixel (carattere monospazio)
static int approxTextWidthPx(const String& s) { return s.length() * CHAR_W; }

// =========================== XML helper e wrapping ===========================
// Rimuove tag XML/HTML, decodifica entità e gestisce blocchi CDATA
static String stripTagsAndDecode(String s) {
  int c0 = s.indexOf("<![CDATA[");
  if (c0 >= 0) { int c1 = s.indexOf("]]>", c0); if (c1 > c0) s = s.substring(c0 + 9, c1); }
  String out; out.reserve(s.length()); bool inTag = false;
  for (size_t i=0;i<s.length();++i) {
    char ch = s[i];
    if (ch == '<') { inTag = true; continue; }
    if (ch == '>') { inTag = false; continue; }
    if (!inTag) out += ch;
  }
  out.replace("&amp;","&"); out.replace("&lt;","<"); out.replace("&gt;",">");
  out.replace("&quot;","'"); out.replace("&apos;","'"); out.replace("&nbsp;"," ");
  out.trim(); while (out.indexOf("  ") >= 0) out.replace("  ", " ");
  return normalizeAndTransliterate(out);
}

// Wrap manuale in un box rettangolare (line breaking per parole/spazi)
static void drawWrappedInBox(int bx, int by, int bw, int bh, const String& text) {
  const int lineHeight = CHAR_H + ITEM_LINE_SP;
  const int maxLines   = (bh - ITEM_MARGIN_TOP*2) / lineHeight;
  const int maxWidth   = bw - ITEM_MARGIN_X*2;

  int y = by + ITEM_MARGIN_TOP + CHAR_H;
  String s = text; int start = 0;

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

// =========================== NVS: lettura/scrittura config feed ===========================
// Carica al più MAX_FEEDS voci: se NVS vuoto, popola con default
static void loadFeedConfig(FeedConfig outFeeds[MAX_FEEDS], int &count) {
  count = 0;
  prefs.begin("rss", true);
  uint8_t n = prefs.getUChar("n", 0);
  if (n == 0) {
    for (int i=0; i<4 && i<MAX_FEEDS; ++i) { outFeeds[i].url = FEEDS_DEFAULT[i]; outFeeds[i].limit = DEFAULT_LIMIT; }
    count = min(4, MAX_FEEDS); prefs.end(); return;
  }
  for (int i=0; i<MAX_FEEDS && i<n; ++i) {
    String u = prefs.getString((String("url")+i).c_str(), "");
    int lim  = prefs.getInt((String("limit")+i).c_str(), DEFAULT_LIMIT);
    if (u.length()) { outFeeds[count].url = u; outFeeds[count].limit = max(1, lim); count++; }
  }
  prefs.end();
  if (count == 0) {
    for (int i=0; i<4 && i<MAX_FEEDS; ++i) { outFeeds[i].url = FEEDS_DEFAULT[i]; outFeeds[i].limit = DEFAULT_LIMIT; }
    count = min(4, MAX_FEEDS);
  }
}

// Salva tutte le righe ricevute dal form /rss (POST). Campi vuoti vengono ignorati.
static void saveFeedConfigFromForm() {
  int n = 0; FeedConfig tmp[MAX_FEEDS];
  for (int i=0; i<MAX_FEEDS; ++i) {
    String ukey = String("url") + i;
    String lkey = String("limit") + i;
    String u = web.hasArg(ukey) ? web.arg(ukey) : "";
    String l = web.hasArg(lkey) ? web.arg(lkey) : "";
    u.trim(); l.trim();
    if (!u.length()) continue;
    int lim = l.length() ? l.toInt() : DEFAULT_LIMIT;
    if (lim <= 0) lim = DEFAULT_LIMIT;
    tmp[n].url = u; tmp[n].limit = lim; n++;
  }
  prefs.begin("rss", false);
  prefs.putUChar("n", (uint8_t)n);
  for (int i=0; i<MAX_FEEDS; ++i) {
    String kurl = String("url") + i, klim = String("limit") + i;
    if (i < n) { prefs.putString(kurl.c_str(), tmp[i].url); prefs.putInt(klim.c_str(), tmp[i].limit); }
    else { prefs.remove(kurl.c_str()); prefs.remove(klim.c_str()); }
  }
  prefs.end();
}

// =========================== HTML: pagine captive e WebUI (UTF-8) ===========================
// AP: modulo di sola configurazione SSID/password, con suggerimento IP locale
static String htmlIndexAP() {
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

// STA: home minima con link a /rss; utile come default di onNotFound
static String htmlIndexSTA() {
  String page =
    "<!doctype html><html><head>"
    "<meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
    "<title>News Ticker</title>"
    "<style>body{font-family:system-ui,Segoe UI,Roboto,Ubuntu,Arial,sans-serif}</style>"
    "</head><body>"
    "<h2>Gat News Ticker</h2>"
    "<p><a href='/rss'>Impostazioni RSS</a></p>"
    "</body></html>";
  return page;
}

// STA: singola pagina /rss. In POST mostra un banner di conferma (senza redirect).
static String htmlRSS(bool saved = false) {
  FeedConfig cfg[MAX_FEEDS]; int n=0; loadFeedConfig(cfg, n);

  String rows;
  for (int i=0; i<MAX_FEEDS; ++i) {
    String u = (i<n) ? cfg[i].url : String("");
    int lim   = (i<n) ? cfg[i].limit : DEFAULT_LIMIT;
    rows +=
      "<tr class='row'>"
      "<td class='drag' title='Trascina per riordinare'>&#8942;&#8942;</td>"
      "<td><input type='text' name='url" + String(i) + "' value='" + u + "' placeholder='https://...'/></td>"
      "<td class='limit'><input type='number' name='limit" + String(i) + "' min='1' max='120' value='" + String(lim) + "'/></td>"
      "<td class='actions'>"
      "<button class='icon up' type='button' onclick='moveUp(this)' title='Su'>&#8593;</button>"
      "<button class='icon down' type='button' onclick='moveDown(this)' title='Giù'>&#8595;</button>"
      "<button class='icon del' type='button' onclick='delRow(this)' title='Rimuovi'>&#10006;</button>"
      "</td></tr>";
  }

  String notice = saved
    ? "<div class='notice'>Impostazioni salvate. La lista news si aggiorna subito.</div>"
    : "";

  String page =
    "<!doctype html><html><head>"
    "<meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>RSS – Gat News Ticker</title>"
    "<style>"
    "body{font-family:system-ui,-apple-system,Segoe UI,Roboto,Ubuntu,Helvetica,Arial,sans-serif;margin:0;background:#0b0b0b;color:#eee}"
    "header{position:sticky;top:0;background:#0b5bd3;padding:14px 16px;color:#ffea00;box-shadow:0 1px 8px rgba(0,0,0,.3)}"
    "header h1{margin:0;font-size:18px}"
    ".notice{margin:12px 0;padding:12px 14px;border-left:4px solid #0dad4a;background:#0f2218;color:#c7ffd9;border-radius:8px}"
    "main{padding:16px;max-width:900px;margin:0 auto}"
    ".card{background:#141414;border:1px solid #262626;border-radius:12px;box-shadow:0 4px 14px rgba(0,0,0,.25)}"
    ".card h2{margin:0;padding:14px 16px;border-bottom:2px solid #0dad4a;font-size:16px;color:#f1f1f1}"
    "table{width:100%;border-collapse:collapse}"
    "th,td{padding:10px 12px;border-bottom:1px solid #222;vertical-align:middle}"
    "th{color:#cfcfcf;font-weight:600;text-align:left}"
    "tr:last-child td{border-bottom:none}"
    "input[type=text]{width:100%;padding:10px 12px;border:1px solid #2b2b2b;background:#0f0f0f;color:#eee;border-radius:8px;outline:none}"
    "input[type=number]{width:110px;padding:10px 12px;border:1px solid #2b2b2b;background:#0f0f0f;color:#eee;border-radius:8px;outline:none}"
    "input:focus{border-color:#0dad4a;box-shadow:0 0 0 3px rgba(13,173,74,.2)}"
    ".toolbar{display:flex;gap:8px;flex-wrap:wrap;padding:12px 16px;border-top:2px solid #0dad4a;background:#101010;border-bottom-left-radius:12px;border-bottom-right-radius:12px}"
    ".btn{appearance:none;border:none;border-radius:10px;padding:10px 14px;font-weight:600;cursor:pointer}"
    ".primary{background:#0dad4a;color:#111}"
    ".ghost{background:transparent;color:#ddd;border:1px solid #2b2b2b}"
    ".danger{background:#2b0b0b;color:#ffb3b3;border:1px solid #4a1111}"
    ".btn:disabled{opacity:.6;cursor:not-allowed}"
    ".row .actions{white-space:nowrap}"
    ".icon{width:36px;height:36px;border-radius:8px;border:1px solid #2b2b2b;background:#121212;color:#ddd;margin-left:6px}"
    ".icon:hover{border-color:#0dad4a}"
    ".drag{width:30px;text-align:center;color:#777;cursor:grab}"
    ".limit{width:130px}"
    "@media(max-width:720px){.drag{display:none}.limit{width:auto}}"
    ".help{color:#aaa;font-size:13px;margin:10px 0 0 0}"
    ".footer{padding:14px 16px;color:#aaa;font-size:12px;text-align:center}"
    "</style>"
    "</head><body>"
    "<header><h1>Impostazioni RSS</h1></header>"
    "<main>"
    + notice +
    "<div class='card'>"
    "<h2>Feed e limiti (max " + String(MAX_FEEDS) + ")</h2>"
    "<form id='f' method='POST' action='/rss' onsubmit='return prepareSubmit()'>"
    "<table id='tbl'>"
    "<thead><tr><th></th><th>URL</th><th>Max elementi</th><th></th></tr></thead>"
    "<tbody id='tbody'>" + rows + "</tbody>"
    "</table>"
    "<div class='toolbar'>"
    "<button type='button' class='btn ghost' onclick='addRow()'>+ Aggiungi feed</button>"
    "<button type='button' class='btn ghost' onclick='fillDefaults()'>Carica default</button>"
    "<button type='button' class='btn danger' onclick='clearAll()'>Svuota tutto</button>"
    "<span class='help'>Suggerimento: riordina con le frecce; i campi vuoti non verranno salvati.</span>"
    "<div style='flex:1'></div>"
    "<button type='submit' class='btn primary'>Salva</button>"
    "<a class='btn ghost' href='/' role='button'>Home</a>"
    "</div>"
    "</form>"
    "</div>"
    "<div class='footer'>Gat News Ticker · WebUI RSS</div>"
    "</main>"
    "<script>"
    "const MAX=" + String(MAX_FEEDS) + ";"
    "const DEF=['" + String(FEEDS_DEFAULT[0]) + "','" + String(FEEDS_DEFAULT[1]) + "','" + String(FEEDS_DEFAULT[2]) + "','" + String(FEEDS_DEFAULT[3]) + "'];"
    "function qs(s,el=document){return el.querySelector(s)}"
    "function qsa(s,el=document){return Array.from(el.querySelectorAll(s))}"
    "function rowTemplate(i,url='',lim=" + String(DEFAULT_LIMIT) + "){"
      "return `<tr class='row'>"
      "<td class='drag' title='Trascina per riordinare'>&#8942;&#8942;</td>"
      "<td><input type='text' name='url${i}' value='${url}' placeholder='https://...'/></td>"
      "<td class='limit'><input type='number' name='limit${i}' min='1' max='120' value='${lim}'/></td>"
      "<td class='actions'>"
      "<button class='icon up' type='button' onclick='moveUp(this)' title='Su'>&#8593;</button>"
      "<button class='icon down' type='button' onclick='moveDown(this)' title='Giù'>&#8595;</button>"
      "<button class='icon del' type='button' onclick='delRow(this)' title='Rimuovi'>&#10006;</button>"
      "</td></tr>`}"
    "function renumber(){qsa('#tbody tr').forEach((tr,idx)=>{qs(\"input[type=text]\",tr).setAttribute('name','url'+idx);qs(\"input[type=number]\",tr).setAttribute('name','limit'+idx);});}"
    "function addRow(){let body=qs('#tbody');let rows=qsa('tr',body).length;if(rows>=MAX)return;body.insertAdjacentHTML('beforeend',rowTemplate(rows,''," + String(DEFAULT_LIMIT) + "));}"
    "function delRow(btn){let tr=btn.closest('tr'); tr.remove(); renumber();}"
    "function moveUp(btn){let tr=btn.closest('tr'); let prev=tr.previousElementSibling; if(prev){tr.parentNode.insertBefore(tr,prev); renumber();}}"
    "function moveDown(btn){let tr=btn.closest('tr'); let next=tr.nextElementSibling; if(next){tr.parentNode.insertBefore(next,tr); renumber();}}"
    "function clearAll(){qs('#tbody').innerHTML=''; addRow();}"
    "function fillDefaults(){qs('#tbody').innerHTML=''; let lim=" + String(DEFAULT_LIMIT) + "; for(let i=0;i<Math.min(DEF.length,MAX);i++){qs('#tbody').insertAdjacentHTML('beforeend',rowTemplate(i,DEF[i],lim));} renumber();}"
    "function prepareSubmit(){let rows=qsa('#tbody tr'); rows.forEach(tr=>{let url=qs(\"input[type=text]\",tr).value.trim(); let lim=qs(\"input[type=number]\",tr); let v=parseInt(lim.value||" + String(DEFAULT_LIMIT) + "); if(isNaN(v)||v<1)v=1; if(v>120)v=120; lim.value=v; if(url===''){ tr.querySelectorAll('input').forEach(i=>i.value=''); }}); renumber(); return true;}"
    "</script>"
    "</body></html>";

  return page;
}

// =========================== Handler HTTP (AP/STA) ===========================
// AP: root, salvataggio credenziali, reboot per migrare in STA
static void handleRootAP()   { web.send(200, "text/html; charset=utf-8", htmlIndexAP()); }

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

static void handleReboot() { web.send(200, "text/plain; charset=utf-8", "OK"); delay(100); ESP.restart(); }

// STA: una sola pagina /rss. GET mostra form, POST salva e conferma inline
static void handleRootSTA() { web.send(200, "text/html; charset=utf-8", htmlIndexSTA()); }

static void handleRSS() {
  bool saved = false;
  if (web.method() == HTTP_POST) {
    saveFeedConfigFromForm();
    g_rssRefreshPending = true;    // segnala al loop di ricaricare e ridisegnare
    saved = true;
  }
  web.send(200, "text/html; charset=utf-8", htmlRSS(saved));
}

// =========================== Avvio server web e captive ===========================
// DNS “*” per forzare il captive portal (risponde con IP dell’AP)
static void startDNSCaptive() { dnsServer.start(DNS_PORT, "*", WiFi.softAPIP()); }

// AP: registra sole route per setup Wi-Fi
static void startAPPortal() {
  web.on("/",       HTTP_GET,  handleRootAP);
  web.on("/save",   HTTP_POST, handleSave);
  web.on("/reboot", HTTP_GET,  handleReboot);
  web.onNotFound(handleRootAP);
  web.begin();
}

// STA: registra home minima e pagina unica /rss (GET/POST)
static void startSTAWebUI() {
  web.on("/",   HTTP_GET,  handleRootSTA);
  web.on("/rss",HTTP_ANY,  handleRSS);
  web.onNotFound(handleRootSTA);
  web.begin();
}

// =========================== UI grafica su display ===========================
// Barra superiore (blu) con titolo e orologio (gialli)
static void drawHeader() {
  gfx->fillRect(0, 0, 480, HEADER_H, RGB565_BLUE);
  drawBoldTextColored(16, 28, "Gat News Ticker", RGB565_YELLOW, RGB565_BLUE);
  String datetime = getFormattedDateTime();
  if (datetime.length() > 0) {
    int textWidth = datetime.length() * CHAR_W;
    int xPos = 480 - textWidth - 16;
    drawBoldTextColored(xPos, 28, datetime, RGB565_YELLOW, RGB565_BLUE);
  }
}

// Schermata di istruzioni quando il dispositivo è in AP (captive attivo)
static void drawAPScreenOnce(const String& ssid, const String& pass) {
  gfx->fillScreen(RGB565_BLACK);
  drawBoldTextColored(16, 36, "Connettiti all'AP:", RGB565_WHITE, RGB565_BLACK);
  drawBoldTextColored(16, 66, ssid,                RGB565_WHITE, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
  gfx->setCursor(16, 96);  gfx->print("Password: "); gfx->print(pass);
  gfx->setCursor(16, 114); gfx->print("Captive portal automatico.");
  gfx->setCursor(16, 126); gfx->print("Se non compare, apri l'IP dell'AP.");
  gfx->setTextSize(TEXT_SCALE);
}

// =========================== Wi-Fi bootstrap ===========================
// Avvia Access Point “PANEL-XXYY”, DNS captive e server web di setup
static void startAPWithPortal() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssidbuf[32]; snprintf(ssidbuf, sizeof(ssidbuf), "PANEL-%02X%02X", mac[4], mac[5]);
  ap_ssid = ssidbuf; ap_pass = "panelsetup";
  WiFi.persistent(false); WiFi.mode(WIFI_AP); WiFi.setSleep(false);
  WiFi.softAP(ap_ssid.c_str(), ap_pass.c_str()); delay(100);
  startDNSCaptive(); startAPPortal(); drawAPScreenOnce(ap_ssid, ap_pass);
}

// Connette in STA usando credenziali da NVS; timeout configurabile
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

// =========================== HTTP/RSS util e parsing ===========================
// Helper status 2xx
static inline bool isHttpOk(int code) { return (code >= 200 && code < 300); }

// Ricerca case-insensitive
static int indexOfCI(const String& s, const String& pat, int from=0) {
  String S = s; S.toLowerCase();
  String P = pat; P.toLowerCase();
  return S.indexOf(P, from);
}

// Estrae <tag>…</tag> a partire da un offset e aggiorna l’indice di scansione
static String extractTag(const String& xml, const String& tag, int& fromIdx) {
  String open = "<" + tag + ">";
  String close = "</" + tag + ">";
  int s = indexOfCI(xml, open, fromIdx); if (s < 0) return "";
  s += open.length();
  int e = indexOfCI(xml, close, s); if (e < 0) return "";
  fromIdx = e + close.length();
  String raw = xml.substring(s, e); raw.trim();
  return raw;
}

// Aggiunge fino a maxAdd item (title+link), elimina duplicati, tenta fallback per feed “atipici”
static int parseFeedItemsLimited(const String& body, int maxAdd) {
  if (maxAdd <= 0) return 0;
  int added = 0; int pos = 0; extractTag(body, "title", pos); // salta titolo canale
  int start;
  while (added < maxAdd && (start = indexOfCI(body, "<item", pos)) >= 0) {
    int openEnd = body.indexOf('>', start); if (openEnd < 0) break;
    int end = indexOfCI(body, "</item>", openEnd); if (end < 0) break;
    String item = body.substring(openEnd + 1, end);
    int ipos = 0; String t = extractTag(item, "title", ipos); ipos = 0; String l = extractTag(item, "link", ipos);
    t = stripTagsAndDecode(t); l.trim();
    if (t.length() && l.length()) {
      bool dup = false; for (int i=0;i<itemCount;i++) { if (titles[i]==t || links[i]==l) { dup=true; break; } }
      if (!dup && itemCount < MAX_ITEMS) { titles[itemCount]=t; links[itemCount]=l; itemCount++; added++; }
    }
    pos = end + 7;
  }
  if (added == 0 && itemCount < MAX_ITEMS) {
    int p = 0; extractTag(body, "title", p);
    for (int i=0; i<maxAdd && itemCount<MAX_ITEMS; ++i) {
      String t = extractTag(body, "title", p);
      String l = extractTag(body, "link",  p);
      if (t.isEmpty() || l.isEmpty()) break;
      t = stripTagsAndDecode(t); l.trim();
      bool dup=false; for (int k=0;k<itemCount;k++) if (titles[k]==t || links[k]==l) { dup=true; break; }
      if (!dup) { titles[itemCount]=t; links[itemCount]=l; itemCount++; added++; if (added>=maxAdd) break; }
    }
  }
  return added;
}

// Mescola gli item (Fisher–Yates) per dare varietà alla rotazione
static void shuffleItems() {
  if (itemCount <= 1) return;
  for (int i = itemCount - 1; i > 0; --i) {
    int j = random(i + 1);
    if (i != j) { String t=titles[i]; titles[i]=titles[j]; titles[j]=t; String l=links[i]; links[i]=links[j]; links[j]=l; }
  }
}

// Scarica e consolida tutti i feed secondo config, quindi azzera la pagina corrente
static void refreshAllFeeds() {
  itemCount = 0;
  FeedConfig cfg[MAX_FEEDS]; int n=0; loadFeedConfig(cfg, n);
  for (int f=0; f<n; ++f) {
    const String &url = cfg[f].url; int lim = max(1, cfg[f].limit);
    if (!url.length()) continue;
    HTTPClient http; http.setTimeout(8000);
    if (!http.begin(url)) continue;
    int code = http.GET();
    if (!isHttpOk(code)) { http.end(); continue; }
    String body = http.getString(); http.end();
    parseFeedItemsLimited(body, lim);
    if (itemCount >= MAX_ITEMS) break;
  }
  shuffleItems();
  currentPage = 0; lastPageSwitch = millis();
}

// =========================== Render della pagina di news ===========================
// Ridisegna intero frame: header + 4 titoli impaginati con separatori verdi
static void drawNewsPage(int pageIdx) {
  gfx->fillScreen(RGB565_BLACK);
  drawHeader();
  for (int i=0; i<ITEMS_PER_PAGE; ++i) {
    int itemIdx = pageIdx * ITEMS_PER_PAGE + i;
    if (itemCount == 0 || itemIdx >= itemCount) break;
    int by = PAGE_Y + i * ITEM_BOX_H;
    if (i > 0) gfx->drawLine(PAGE_X, by, PAGE_X + PAGE_W, by, RGB565_GREEN);
    drawWrappedInBox(PAGE_X, by, PAGE_W, ITEM_BOX_H, titles[itemIdx]);
  }
  int totalPages = (itemCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE; if (totalPages == 0) totalPages = 1;
  char buf[32]; snprintf(buf, sizeof(buf), "%d/%d", (pageIdx % totalPages) + 1, totalPages);
  gfx->setTextSize(1); gfx->setTextColor(RGB565_WHITE, RGB565_BLACK); gfx->setCursor(440, 470); gfx->print(buf);
  gfx->setTextSize(TEXT_SCALE);
}

// =========================== Ciclo di vita: setup/loop ===========================
// Setup: avvia pannello, tenta STA, altrimenti AP. In STA: tempo NTP, WebUI, primo fetch.
void setup() {
  Serial.begin(115200);
  backlightOn(); panelKickstart();
  randomSeed(esp_timer_get_time());
  gfx->fillScreen(RGB565_BLACK);
  drawBoldTextColored(16, 36, "Connessione alla rete...", RGB565_WHITE, RGB565_BLACK);

  if (!tryConnectSTA(8000)) {
    startAPWithPortal();
  } else {
    startSTAWebUI();
    syncTimeFromNTP();
    refreshAllFeeds(); lastRefresh = millis();
    drawNewsPage(currentPage);
  }
}

// Loop: gestisce captive o WebUI, riconnessioni, refresh immediati e periodici, paging
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
      if (tryConnectSTA(5000)) { if (!g_timeSynced) syncTimeFromNTP(); }
    }
    delay(40);
    return;
  }

  if (g_rssRefreshPending) {
    g_rssRefreshPending = false;
    refreshAllFeeds();
    drawNewsPage(currentPage);
  }

  if (millis() - lastRefresh >= REFRESH_INTERVAL_MS) {
    lastRefresh = millis();
    refreshAllFeeds();
    drawNewsPage(currentPage);
  }

  if (millis() - lastPageSwitch >= PAGE_DURATION_MS) {
    lastPageSwitch = millis();
    int totalPages = (itemCount + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE; if (totalPages == 0) totalPages = 1;
    currentPage = (currentPage + 1) % totalPages;
    drawNewsPage(currentPage);
  }
  delay(5);
}
