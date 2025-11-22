#pragma once

/*
===============================================================================
   SQUARED — PAGINA "NEWS" (RSS, prime 5 notizie)
   Versione BILANCIATA
   - testo più piccolo (TEXT_SCALE)
   - spacing ridotto
   - word-wrap pulito
===============================================================================
*/

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include "../handlers/globals.h"

// ====================== EXTERN ======================
extern Arduino_RGB_Display* gfx;
extern const uint16_t COL_BG, COL_TEXT, COL_ACCENT1;
extern const int PAGE_X, PAGE_Y, CHAR_H, BASE_CHAR_W, BASE_CHAR_H, TEXT_SCALE;

extern String g_lang;
extern String g_rss_url;

extern bool httpGET(const String&, String&, uint32_t);
extern String sanitizeText(const String&);
extern void drawHeader(const String&);
extern void drawHLine(int y);

// ====================== CONFIG ======================
static const uint8_t NEWS_MAX = 5;
static String news_title[NEWS_MAX];

/* ============================================================================
   stripTags
============================================================================ */
static inline String stripTags(const String& s) {
  String out;
  out.reserve(s.length());
  bool tag = false;

  for (uint16_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '<') { tag = true; continue; }
    if (c == '>') { tag = false; continue; }
    if (!tag) out += c;
  }
  return out;
}

/* ============================================================================
   decodeEntities
============================================================================ */
static inline void decodeEntities(String& s) {
  s.replace("&amp;", "&");
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&quot;", "\"");
  s.replace("&apos;", "'");
  s.replace("&#39;", "'");
}

/* ============================================================================
   cleanTitle
============================================================================ */
static String cleanTitle(const String& raw) {
  String s = raw;

  if (s.startsWith("<![CDATA[")) {
    s.remove(0, 9);
    int p = s.indexOf("]]>");
    if (p > 0) s = s.substring(0, p);
  }

  s = stripTags(s);
  decodeEntities(s);
  s = sanitizeText(s);
  s.trim();

  return s;
}

/* ============================================================================
   getTag
============================================================================ */
static inline bool getTag(const String& src, const char* tag, String& out) {
  String open  = "<";  open  += tag; open  += ">";
  String close = "</"; close += tag; close += ">";

  int p = src.indexOf(open);
  if (p < 0) return false;

  int s = p + open.length();
  int e = src.indexOf(close, s);
  if (e < 0) return false;

  out = src.substring(s, e);
  out.trim();
  return true;
}

/* ============================================================================
   FETCH RSS
============================================================================ */
bool fetchNews() {

  for (uint8_t i = 0; i < NEWS_MAX; i++)
    news_title[i] = "";

  String url = g_rss_url.length() ? g_rss_url
                                  : "https://feeds.bbci.co.uk/news/rss.xml";

  String body;
  if (!httpGET(url, body, 10000)) return false;
  if (!body.length()) return false;

  uint16_t pos = 0;
  uint8_t count = 0;

  while (count < NEWS_MAX) {

    int a = body.indexOf("<item", pos);
    if (a < 0) break;

    int openEnd = body.indexOf('>', a);
    if (openEnd < 0) break;

    int b = body.indexOf("</item>", openEnd);
    if (b < 0) break;

    String blk = body.substring(openEnd + 1, b);
    pos = b + 7;

    String title;
    if (!getTag(blk, "title", title)) continue;

    title = cleanTitle(title);
    if (!title.length()) continue;

    news_title[count++] = title;
  }

  return count > 0;
}

/* ============================================================================
   RENDER — versione bilanciata
============================================================================ */
void pageNews() {

  const bool it = (g_lang == "it");
  drawHeader(it ? "Notizie" : "News");

  // testo normale
  const uint8_t SZ = TEXT_SCALE;
  gfx->setTextSize(SZ);

  // padding
  const int leftPad  = PAGE_X + 10;
  const int rightPad = 470;

  const int charW = BASE_CHAR_W * SZ;
  const int maxChars = (rightPad - leftPad) / charW;

  int y = PAGE_Y + 8;
  const int lineH = (CHAR_H * SZ) + 6;     // più compatto

  if (!news_title[0].length()) {
    gfx->setCursor(leftPad, y + lineH);
    gfx->setTextColor(COL_ACCENT1, COL_BG);
    gfx->print(it ? "Nessuna notizia" : "No news available");
    return;
  }

  for (uint8_t i = 0; i < NEWS_MAX; i++) {

    if (!news_title[i].length()) continue;

    String text = news_title[i];
    int start = 0;
    int len = text.length();

    // ---- WORD WRAP ----
    while (start < len) {

      int remaining = len - start;
      int take = min(remaining, maxChars);

      int cut = start + take;

      // evita taglio parole
      if (cut < len) {
        int lastSpace = text.lastIndexOf(' ', cut);
        if (lastSpace > start) cut = lastSpace;
      }

      String line = text.substring(start, cut);
      line.trim();

      gfx->setCursor(leftPad, y);
      gfx->setTextColor(COL_TEXT, COL_BG);
      gfx->print(line);

      y += lineH;
      if (y > 440) break;

      start = (cut < len && text[cut] == ' ') ? cut + 1 : cut;
    }

    // separatore compatto
    if (i < NEWS_MAX - 1 && y < 430) {
      drawHLine(y - 3);
      y += 4;
    }

    if (y > 440) break;
  }

  gfx->setTextSize(TEXT_SCALE);
}

