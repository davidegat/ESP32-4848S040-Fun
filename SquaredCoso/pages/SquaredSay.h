#pragma once

/* ============================================================================
   QOD (Quote Of the Day) – modulo completo, ripulito da apici “strani”
   - Niente virgolette tipografiche Unicode (che sul pannello diventano '?')
   - Solo ASCII per virgolette e trattini
============================================================================ */

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "../handlers/globals.h"

// ===================== EXTERN DAL MAIN / ALTRI MODULI =====================
extern Arduino_RGB_Display* gfx;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int PAGE_W;
extern const int PAGE_H;
extern const int CHAR_H;
extern const int BASE_CHAR_W;
extern const int BASE_CHAR_H;
extern const int TEXT_SCALE;

extern const uint16_t COL_BG;
extern const uint16_t COL_ACCENT1;

extern String g_lang;
extern String g_oa_key;
extern String g_oa_topic;

extern bool   httpGET(const String& url, String& body, uint32_t timeoutMs);
extern bool   isHttpOk(int code);
extern int    indexOfCI(const String& src, const String& key, int from);
extern bool   jsonFindStringKV(const String& body,
                               const String& key,
                               int from,
                               String& outVal);
extern String jsonEscape(const String& in);
extern String decodeJsonUnicode(const String& in);
extern String sanitizeText(const String& in);
extern void   todayYMD(String& ymd);
extern void   drawHeader(const String& title);
extern void   drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);
extern void   drawParagraph(int16_t x, int16_t y, int16_t w,
                            const String& text, uint8_t scale);
extern void   drawCurrentPage();

extern WebServer web;


// ===================== CACHE LOCALE =====================
static String qod_text;        // frase
static String qod_author;      // autore
static String qod_date_ymd;    // data cache (YYYYMMDD)
static bool   qod_from_ai = false;   // true = OpenAI, false = ZenQuotes


// ===================== NORMALIZZAZIONE APICI / PUNTEGGIATURA =====================
static String qodSanitizeQuotes(const String& in) {
  String s = in;

  // virgolette tipografiche → ASCII
  s.replace("“", "\"");
  s.replace("”", "\"");
  s.replace("„", "\"");
  s.replace("«", "\"");
  s.replace("»", "\"");

  // apici tipografici → apostrofo semplice
  s.replace("‘", "'");
  s.replace("’", "'");

  // trattini lunghi → trattino semplice
  s.replace("—", "-");
  s.replace("–", "-");

  return s;
}


// ===================== ZENQUOTES =====================
static bool fetchQOD_ZenQuotes() {
  String body;
  if (!httpGET("https://zenquotes.io/api/today", body, 10000))
    return false;

  String q, a;
  if (!jsonFindStringKV(body, "q", 0, q))
    return false;
  jsonFindStringKV(body, "a", 0, a);

  qod_text   = qodSanitizeQuotes(sanitizeText(q));
  qod_author = qodSanitizeQuotes(sanitizeText(a));

  if (qod_text.length() > 280)
    qod_text.remove(277);   // taglio se troppo lunga

  return true;
}


// ===================== OPENAI =====================
static bool fetchQOD_OpenAI() {

  if (!g_oa_key.length() || !g_oa_topic.length())
    return false;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(10000);

  if (!http.begin(client, "https://api.openai.com/v1/responses"))
    return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + g_oa_key);

  String prompt;
  if (g_lang == "it") {
    prompt =
      "Scrivi una breve frase in stile " + g_oa_topic +
      " in buon italiano e senza errori. Solo la frase.";
  } else {
    prompt =
      "Write a short quote in the style of " + g_oa_topic +
      " in good English without errors. Only the quote.";
  }

  String body =
    "{\"model\":\"gpt-4.1-nano\",\"max_output_tokens\":50,\"input\":\"" +
    jsonEscape(prompt) + "\"}";

  int code = http.POST(body);
  if (!isHttpOk(code)) {
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  int pos = indexOfCI(resp, "\"text\"");
  if (pos < 0) return false;

  String raw;
  if (!jsonFindStringKV(resp, "text", pos, raw))
    return false;

  raw = decodeJsonUnicode(raw);
  raw.replace("\\n", " ");
  raw.replace("\\", "");

  raw.trim();
  while (raw.startsWith("\"")) raw.remove(0, 1);
  while (raw.endsWith("\""))  raw.remove(raw.length() - 1);

  // sanificazione completa (HTML/UTF-8 + apici)
  qod_text = qodSanitizeQuotes(sanitizeText(raw));
  if (qod_text.length() > 280)
    qod_text.remove(277);

  qod_author = "AI Generated";
  return qod_text.length() > 0;
}


// ===================== LOGICA MASTER QOD =====================
static bool fetchQOD() {

  String today;
  todayYMD(today);

  bool wantAI = (g_oa_key.length() && g_oa_topic.length());

  // cache valida per il giorno corrente, con stessa "fonte" (AI/non-AI)
  if (qod_text.length() && qod_date_ymd == today) {
    if ((wantAI && qod_from_ai) || (!wantAI && !qod_from_ai))
      return true;
  }

  // reset cache
  qod_text = "";
  qod_author = "";

  // preferisci AI se configurata
  if (wantAI) {
    if (fetchQOD_OpenAI()) {
      qod_date_ymd = today;
      qod_from_ai = true;
      return true;
    }
  }

  // fallback ZenQuotes
  if (fetchQOD_ZenQuotes()) {
    qod_date_ymd = today;
    qod_from_ai = false;
    return true;
  }

  return false;
}


// ===================== HANDLER WEB (rigenera frase) =====================
static void handleForceQOD() {

  qod_text.clear();
  qod_author.clear();
  qod_date_ymd.clear();
  qod_from_ai = false;

  fetchQOD();
  drawCurrentPage();

  web.send(200, "text/html; charset=utf-8",
    "<!doctype html><meta charset='utf-8'><body>"
    "<h3>Frase rigenerata</h3>"
    "<p><a href='/settings'>Back</a></p>"
    "</body>");
}


// ===================== RENDERING PAGINA =====================
static void pageQOD() {

  drawHeader(g_lang == "it" ? "Frase del giorno" : "Quote of the Day");
  int y = PAGE_Y;

  if (!qod_text.length()) {
    drawBoldMain(
      PAGE_X,
      y + CHAR_H,
      g_lang == "it" ? "Nessuna frase disponibile"
                     : "No quote available",
      TEXT_SCALE + 1
    );
    return;
  }

  // scelta scala in base alla lunghezza
  uint16_t L = qod_text.length();
  uint8_t scale = (L < 80 ? 4 : (L < 160 ? 3 : 2));

  // virgolette ASCII, non tipografiche
  String full = "\"" + qod_text + "\"";
  drawParagraph(PAGE_X, y, PAGE_W, full, scale);

  // autore
  String author;
  if (qod_author.length()) {
    author = "- " + qod_author;
  } else {
    author = (g_lang == "it" ? "- sconosciuto" : "- unknown");
  }

  uint8_t aScale = (author.length() < 18 ? 3 : 2);

  int aW = author.length() * BASE_CHAR_W * aScale;
  int aX = (480 - aW) / 2;
  int aY = PAGE_Y + PAGE_H - (BASE_CHAR_H * aScale) - 8;

  gfx->setTextColor(COL_ACCENT1, COL_BG);
  gfx->setTextSize(aScale);
  gfx->setCursor(aX, aY);
  gfx->print(author);

  gfx->setTextSize(TEXT_SCALE);
}

