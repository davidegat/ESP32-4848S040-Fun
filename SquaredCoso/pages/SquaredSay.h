#pragma once

/*
===============================================================================
   QOD – Quote Of The Day (versione ultra-ottimizzata)
   - Minimo uso di RAM (quasi tutto su stack)
   - Nessun carattere Unicode, solo ASCII
   - Nessuna String temporanea oltre al necessario
   - JSON parser ridotto al minimo essenziale
   - Adatta all’ESP32-S3 (heap piccolo)
===============================================================================
*/

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "../handlers/globals.h"

// ========================= EXTERN =========================
extern Arduino_RGB_Display* gfx;
extern WebServer web;

extern const int PAGE_X, PAGE_Y, PAGE_W, PAGE_H;
extern const int CHAR_H, BASE_CHAR_W, BASE_CHAR_H, TEXT_SCALE;
extern const uint16_t COL_BG, COL_ACCENT1;

extern String g_lang, g_oa_key, g_oa_topic;

extern bool   httpGET(const String&, String&, uint32_t);
extern void   todayYMD(String&);
extern void   drawHeader(const String&);
extern void   drawBoldMain(int16_t, int16_t, const String&, uint8_t);
extern void   drawParagraph(int16_t, int16_t, int16_t, const String&, uint8_t);
extern void   drawCurrentPage();


// ========================= CACHE =========================
// Due sole String in RAM → minimo impatto
static String qod_text;
static String qod_author;
static String qod_date_ymd;
static bool   qod_from_ai = false;


// ==========================================================
// 1) Normalizzazione virgolette in puro ASCII (leggera)
// ==========================================================
static inline void normalizeQuotes(String &s)
{
  // Tutto ASCII → compatibilità totale col pannello
  s.replace("“", "\"");
  s.replace("”", "\"");
  s.replace("‘", "'");
  s.replace("’", "'");
  s.replace("–", "-");
  s.replace("—", "-");
}


// ==========================================================
// 2) Mini-parser JSON: prende il valore di "key":"value"
//    Ultra veloce, zero parsing generico
// ==========================================================
static bool tinyJsonGet(const String& src,
                        const char* key,
                        String& out)
{
  // JSON semplice → cerchiamo "<key>":
  char pat[32];
  snprintf(pat, sizeof(pat), "\"%s\":\"", key);

  int p = src.indexOf(pat);
  if (p < 0) return false;

  p += strlen(pat);             // dopo "key":"  
  int q = src.indexOf('"', p);  // fino alla chiusura

  if (q < 0) return false;

  out = src.substring(p, q);
  return true;
}


// ==========================================================
// 3) ZENQUOTES – leggerissima
// ==========================================================
static bool fetchQOD_ZQ()
{
  String body;
  if (!httpGET("https://zenquotes.io/api/today", body, 9000))
    return false;

  String q, a;
  if (!tinyJsonGet(body, "q", q)) return false;
  tinyJsonGet(body, "a", a);

  normalizeQuotes(q);
  normalizeQuotes(a);

  qod_text   = q;
  qod_author = a.length() ? a : "unknown";

  if (qod_text.length() > 240)
    qod_text.remove(240);

  return true;
}


// ==========================================================
// 4) OpenAI – semplificata, zero sprechi
// ==========================================================
static bool fetchQOD_AI()
{
  if (!g_oa_key.length() || !g_oa_topic.length())
    return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, "https://api.openai.com/v1/responses"))
    return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + g_oa_key);

  // prompt minimale
  String prompt =
    (g_lang == "it" ?
      "Scrivi una breve frase in stile " + g_oa_topic + ". Solo la frase."
    :
      "Write a short quote in the style of " + g_oa_topic + ". Only the quote.");

  // JSON leggerissimo (niente escape complessi)
  String body =
    "{\"model\":\"gpt-4.1-nano\",\"input\":\"" +
    prompt + "\",\"max_output_tokens\":50}";

  int code = http.POST(body);
  if (code != 200) {
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  // cerco "text":"...”
  int p = resp.indexOf("\"text\":\"");
  if (p < 0) return false;

  p += 8;
  int q = resp.indexOf('"', p);
  if (q < 0) return false;

  String t = resp.substring(p, q);

  // rimozione safe
  t.replace("\\n", " ");
  t.replace("\\", "");
  normalizeQuotes(t);

  t.trim();
  if (!t.length()) return false;

  if (t.length() > 240)
    t.remove(240);

  qod_text   = t;
  qod_author = "AI";
  return true;
}


// ==========================================================
// 5) LOGICA PRINCIPALE + CACHE
// ==========================================================
static bool fetchQOD()
{
  String today;
  todayYMD(today);

  bool wantAI = (g_oa_key.length() && g_oa_topic.length());

  // cache stessa fonte
  if (qod_text.length() && qod_date_ymd == today) {
    if ((wantAI && qod_from_ai) || (!wantAI && !qod_from_ai))
      return true;
  }

  // reset
  qod_text   = "";
  qod_author = "";

  // AI → prioritaria
  if (wantAI && fetchQOD_AI()) {
    qod_from_ai  = true;
    qod_date_ymd = today;
    return true;
  }

  // fallback
  if (fetchQOD_ZQ()) {
    qod_from_ai  = false;
    qod_date_ymd = today;
    return true;
  }

  return false;
}


// ==========================================================
// 6) Web handler: rigenera
// ==========================================================
static void handleForceQOD()
{
  qod_text.clear();
  qod_author.clear();
  qod_date_ymd.clear();
  qod_from_ai = false;

  fetchQOD();
  drawCurrentPage();

  web.send(200, "text/html",
    "<html><body><h3>Regenerata</h3>"
    "<a href='/settings'>Back</a></body></html>");
}


// ==========================================================
// 7) RENDER – leggerissimo
// ==========================================================
static void pageQOD()
{
  drawHeader(g_lang == "it" ? "Frase del giorno" : "Quote of the day");

  if (!qod_text.length()) {
    drawBoldMain(
      PAGE_X,
      PAGE_Y + CHAR_H,
      (g_lang == "it" ? "Nessuna frase" : "No quote"),
      TEXT_SCALE + 1
    );
    return;
  }

  // scala dinamica
  int L = qod_text.length();
  uint8_t scale = (L < 80 ? 4 : (L < 160 ? 3 : 2));

  // testo con virgolette ASCII
  String full = "\"" + qod_text + "\"";

  drawParagraph(PAGE_X, PAGE_Y, PAGE_W, full, scale);

  // autore
  String a = "- " + (qod_author.length() ? qod_author :
                     (g_lang == "it" ? "sconosciuto" : "unknown"));

  uint8_t aScale = (a.length() < 18 ? 3 : 2);

  int w  = a.length() * BASE_CHAR_W * aScale;
  int ax = (480 - w) / 2;
  int ay = PAGE_Y + PAGE_H - (BASE_CHAR_H * aScale) - 6;

  gfx->setTextColor(COL_ACCENT1, COL_BG);
  gfx->setTextSize(aScale);
  gfx->setCursor(ax, ay);
  gfx->print(a);
  gfx->setTextSize(TEXT_SCALE);
}

