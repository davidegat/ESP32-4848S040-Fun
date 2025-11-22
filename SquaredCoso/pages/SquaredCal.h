#pragma once
#include <Arduino.h>
#include <time.h>

/*
===============================================================================
   MODULO: CALENDARIO ICS (eventi del giorno, SENZA LUOGO)

   Funzioni:
     • fetchICS()      – scarica e filtra gli eventi ICS della giornata
     • pageCalendar()  – disegna gli eventi ordinati per orario

   Caratteristiche:
     - Parsing ICS minimale (ricerca stringhe)
     - Mostra massimo 3 eventi
     - Filtra solo gli eventi del giorno (YYYYMMDD)
     - Supporto eventi All-Day e con orario
     - Nessun campo luogo → interfaccia più pulita
===============================================================================
*/


// ============================================================================
// EXTERN DAL MAIN
// ============================================================================
extern String g_lang;
extern String g_ics;

extern void todayYMD(String& ymd);
extern bool httpGET(const String& url, String& body, uint32_t timeoutMs);
extern int indexOfCI(const String& src, const String& key, int from);

extern void drawHeader(const String& title);
extern void drawBoldMain(int16_t x, int16_t y, const String& raw, uint8_t scale);
extern String sanitizeText(const String& in);

extern Arduino_RGB_Display* gfx;

extern const int PAGE_X;
extern const int PAGE_Y;
extern const int CHAR_H;
extern const int TEXT_SCALE;

extern const uint16_t COL_TEXT;
extern const uint16_t COL_BG;

extern void drawHLine(int y);


// ============================================================================
// STRUTTURA EVENTO ICS (senza luogo)
// ============================================================================
struct CalItem {
  String when;         // "HH:MM" o "tutto il giorno"
  String summary;      // titolo
  time_t ts;           // timestamp
  bool allDay;         // true = evento senza ora
};

static CalItem cal[3];


// ============================================================================
// HELPERS ICS
// ============================================================================
static inline void resetCal() {
  for (uint8_t i = 0; i < 3; i++) {
    cal[i].when    = "";
    cal[i].summary = "";
    cal[i].ts      = 0;
    cal[i].allDay  = false;
  }
}

static inline String trimField(const String& s) {
  String t = s;
  t.trim();
  return t;
}

static inline String extractAfterColon(const String& src, int pos) {
  int c = src.indexOf(':', pos);
  if (c < 0) return "";
  int e = src.indexOf('\n', c + 1);
  if (e < 0) e = src.length();
  return trimField(src.substring(c + 1, e));
}

static inline bool isTodayStamp(const String& dtstamp, const String& todayYmd) {
  return dtstamp.length() >= 8 && dtstamp.startsWith(todayYmd);
}

static inline void humanTimeFromStamp(const String& stamp, String& out) {
  if (stamp.length() >= 15 && stamp[8] == 'T') {
    out = stamp.substring(9, 11) + ":" + stamp.substring(11, 13);
  } else {
    out = (g_lang == "it" ? "tutto il giorno" : "all day");
  }
}


// ============================================================================
// FETCH ICS – filtra eventi del giorno (SENZA luogo)
// ============================================================================
bool fetchICS() {

  resetCal();

  if (!g_ics.length())
    return true;

  String body;
  if (!httpGET(g_ics, body, 15000))
    return false;

  String today;
  todayYMD(today);

  uint8_t idx = 0;
  int p = 0;

  while (idx < 3) {

    int b = body.indexOf("BEGIN:VEVENT", p);
    if (b < 0) break;

    int e = body.indexOf("END:VEVENT", b);
    if (e < 0) break;

    String blk = body.substring(b, e);

    // DTSTART
    int ds = indexOfCI(blk, "DTSTART");
    if (ds < 0) { p = e + 10; continue; }

    String rawStart = extractAfterColon(blk, ds);
    if (!isTodayStamp(rawStart, today)) {
      p = e + 10;
      continue;
    }

    // SUMMARY
    int ss = indexOfCI(blk, "SUMMARY");
    String summary = (ss >= 0) ? extractAfterColon(blk, ss) : "";
    if (!summary.length()) { p = e + 10; continue; }

    // Orario human-readable
    String when;
    humanTimeFromStamp(rawStart, when);

    // Converte in time_t
    struct tm tt = {};
    tt.tm_year = rawStart.substring(0, 4).toInt() - 1900;
    tt.tm_mon  = rawStart.substring(4, 6).toInt() - 1;
    tt.tm_mday = rawStart.substring(6, 8).toInt();

    bool hasTime = (rawStart.length() >= 15 && rawStart[8] == 'T');
    if (hasTime) {
      tt.tm_hour = rawStart.substring(9, 11).toInt();
      tt.tm_min  = rawStart.substring(11, 13).toInt();
    }

    // Salva evento
    cal[idx].when    = sanitizeText(when);
    cal[idx].summary = sanitizeText(summary);
    cal[idx].ts      = mktime(&tt);
    cal[idx].allDay  = !hasTime;

    idx++;
    p = e + 10;
  }

  return true;
}


// ============================================================================
// PAGE CALENDAR — titolo + orario molto vicini (senza sovrapposizione)
// ============================================================================
void pageCalendar() {

  // Titolo pagina
  drawHeader(
    g_lang == "it" ? "Calendario (oggi)"
                   : "Today's calendar"
  );

  int y = PAGE_Y;

  // Buffer righe ordinabili
  struct Row {
    String when, summary;
    time_t ts;
    long delta;
    bool allDay;
  } rows[3];

  uint8_t n = 0;
  time_t now;
  time(&now);

  // Copia eventi validi
  for (uint8_t i = 0; i < 3; i++) {

    if (!cal[i].summary.length())
      continue;

    long d = cal[i].allDay ? 0 : difftime(cal[i].ts, now);
    if (d < 0) d = 86400;   // eventi già passati → fondo lista

    rows[n].when    = cal[i].when;
    rows[n].summary = cal[i].summary;
    rows[n].ts      = cal[i].ts;
    rows[n].delta   = d;
    rows[n].allDay  = cal[i].allDay;

    n++;
  }

  // Nessun evento
  if (!n) {
    drawBoldMain(PAGE_X, y + CHAR_H,
                 g_lang == "it" ? "Nessun evento"
                                : "No events today",
                 TEXT_SCALE + 1);
    return;
  }

  // Ordina per orario (delta crescente)
  for (uint8_t i = 0; i < n - 1; i++)
    for (uint8_t j = i + 1; j < n; j++)
      if (rows[j].delta < rows[i].delta)
        std::swap(rows[i], rows[j]);


  // Rendering
  for (uint8_t i = 0; i < n; i++) {

    // Titolo evento
    drawBoldMain(PAGE_X, y, rows[i].summary, TEXT_SCALE + 1);

    // Avvicina l’orario al titolo (vicinissimo, non sovrapposto)
    y += (CHAR_H * (TEXT_SCALE + 1)) - 6;

    // Orario
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT, COL_BG);
    gfx->setCursor(PAGE_X, y);
    gfx->print(rows[i].when);

    // Spazio dopo orario
    y += CHAR_H * 2 + 4;
    gfx->setTextSize(TEXT_SCALE);

    // Separatore tra eventi
    if (i < n - 1) {
      drawHLine(y);
      y += 12;
    }

    if (y > 450)
      break;
  }
}

