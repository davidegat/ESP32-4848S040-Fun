#pragma once
#include <Arduino.h>
#include <time.h>

/*
===============================================================================
   MODULO: CALENDARIO ICS (eventi del giorno, SENZA LUOGO) — HYPER OPTIMIZED

   Funzioni esposte:
     • fetchICS()      – scarica e filtra gli eventi ICS della giornata
     • pageCalendar()  – disegna gli eventi ordinati per orario

   Caratteristiche:
     - Parsing ICS minimale (ricerca stringhe)
     - Mostra massimo 3 eventi
     - Filtra solo gli eventi del giorno (YYYYMMDD)
     - Supporto eventi All-Day e con orario
     - Nessun campo luogo → interfaccia più pulita
     - Ridotto uso di String persistenti: cal[] usa buffer char statici
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
//  - nessuna String persistente → meno frammentazione heap
//  - campi testuali limitati da buffer fissi
// ============================================================================
struct CalItem {
  char   when[16];      // "HH:MM" o "tutto il giorno"/"all day" (troncato se troppo lungo)
  char   summary[64];   // titolo evento (troncato)
  time_t ts;            // timestamp inizio
  bool   allDay;        // true = evento senza ora
  bool   used;          // true = slot occupato valido
};

static CalItem cal[3];


// ============================================================================
// HELPERS ICS
// ============================================================================

// reset cal[] a stato "nessun evento"
static inline void resetCal() {
  for (uint8_t i = 0; i < 3; i++) {
    cal[i].when[0]    = '\0';
    cal[i].summary[0] = '\0';
    cal[i].ts         = 0;
    cal[i].allDay     = false;
    cal[i].used       = false;
  }
}

// Estrarre testo dopo il primo ':' fino a fine riga ('\n'), con trim
static inline String extractAfterColon(const String& src, int pos) {
  int c = src.indexOf(':', pos);
  if (c < 0) return "";
  int e = src.indexOf('\n', c + 1);
  if (e < 0) e = src.length();

  String t = src.substring(c + 1, e);
  t.trim();
  return t;
}

// Controlla se uno stampo ICS (YYYYMMDD...) è relativo a "oggi"
static inline bool isTodayStamp(const String& dtstamp, const String& todayYmd) {
  return (dtstamp.length() >= 8) && dtstamp.startsWith(todayYmd);
}

// Converte timestamp ICS in stringa human-readable per l’orario
static inline void humanTimeFromStamp(const String& stamp, String& out) {
  if (stamp.length() >= 15 && stamp[8] == 'T') {
    // Formato tipico: YYYYMMDDTHHMMSSZ o simile
    out = stamp.substring(9, 11) + ":" + stamp.substring(11, 13);
  } else {
    // Evento senza ora → all day
    out = (g_lang == "it" ? "tutto il giorno" : "all day");
  }
}

// Copia sicura da String a buffer char con troncamento
static inline void copyToBuf(const String& s, char* buf, size_t bufLen) {
  if (!bufLen) return;
  size_t n = s.length();
  if (n >= bufLen) n = bufLen - 1;
  for (size_t i = 0; i < n; i++) buf[i] = (char)s[i];
  buf[n] = '\0';
}


// ============================================================================
// FETCH ICS – filtra eventi del giorno (SENZA luogo)
//   - Usa String solo come buffer temporaneo per HTTP e parsing
//   - cal[] conserva solo char[] + time_t (nessuna String persistente)
// ============================================================================
bool fetchICS() {

  resetCal();

  // Nessun URL ICS configurato → consideriamo "ok" ma senza eventi
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
    // Blocchi VEVENT
    int b = body.indexOf("BEGIN:VEVENT", p);
    if (b < 0) break;

    int e = body.indexOf("END:VEVENT", b);
    if (e < 0) break;

    String blk = body.substring(b, e);

    // DTSTART (data inizio evento)
    int ds = indexOfCI(blk, "DTSTART", 0);
    if (ds < 0) { p = e + 10; continue; }

    String rawStart = extractAfterColon(blk, ds);
    if (!isTodayStamp(rawStart, today)) {
      // Evento non di oggi → salta
      p = e + 10;
      continue;
    }

    // SUMMARY (titolo)
    int ss = indexOfCI(blk, "SUMMARY", 0);
    String summary = (ss >= 0) ? extractAfterColon(blk, ss) : "";
    if (!summary.length()) { p = e + 10; continue; }

    // Orario human-readable
    String whenStr;
    humanTimeFromStamp(rawStart, whenStr);

    // Sanitize testi per evitare caratteri strani
    summary = sanitizeText(summary);
    whenStr = sanitizeText(whenStr);

    // Converte timestamp ICS in struct tm
    struct tm tt = {};
    if (rawStart.length() >= 8) {
      tt.tm_year = rawStart.substring(0, 4).toInt() - 1900;
      tt.tm_mon  = rawStart.substring(4, 6).toInt() - 1;
      tt.tm_mday = rawStart.substring(6, 8).toInt();
    }

    bool hasTime = (rawStart.length() >= 15 && rawStart[8] == 'T');
    if (hasTime) {
      tt.tm_hour = rawStart.substring(9, 11).toInt();
      tt.tm_min  = rawStart.substring(11, 13).toInt();
    } else {
      tt.tm_hour = 0;
      tt.tm_min  = 0;
    }

    // Riempie slot cal[idx] usando buffer statici
    copyToBuf(whenStr,   cal[idx].when,    sizeof(cal[idx].when));
    copyToBuf(summary,   cal[idx].summary, sizeof(cal[idx].summary));
    cal[idx].ts     = mktime(&tt);
    cal[idx].allDay = !hasTime;
    cal[idx].used   = true;

    idx++;
    p = e + 10;
  }

  return true;
}


// ============================================================================
// PAGE CALENDAR — titolo + orario molto vicini (senza sovrapposizione)
//   - Ordina max 3 eventi per “quanto manca” (delta) rispetto a ora
//   - Usa struct Row con soli indici → niente String extra
// ============================================================================
void pageCalendar() {

  // Titolo pagina
  drawHeader(
    g_lang == "it" ? "Calendario (oggi)"
                   : "Today's calendar"
  );

  int y = PAGE_Y;

  // Struttura per l’ordinamento
  struct Row {
    uint8_t idx;    // indice in cal[]
    time_t  ts;
    long    delta;  // quanto manca rispetto a now (allDay = 0)
    bool    allDay;
  };

  Row rows[3];
  uint8_t n = 0;

  time_t now;
  time(&now);

  // Costruisce lista di eventi validi
  for (uint8_t i = 0; i < 3; i++) {
    if (!cal[i].used || cal[i].summary[0] == '\0')
      continue;

    long d = cal[i].allDay ? 0 : (long)difftime(cal[i].ts, now);
    if (d < 0) d = 86400;   // eventi già passati → fondo lista

    rows[n].idx    = i;
    rows[n].ts     = cal[i].ts;
    rows[n].delta  = d;
    rows[n].allDay = cal[i].allDay;
    n++;
  }

  // Nessun evento → messaggio semplice
  if (!n) {
    drawBoldMain(
      PAGE_X,
      y + CHAR_H,
      (g_lang == "it" ? "Nessun evento" : "No events today"),
      TEXT_SCALE + 1
    );
    return;
  }

  // Ordina per delta crescente (insertion sort semplice su max 3 elementi)
  for (uint8_t i = 1; i < n; i++) {
    Row key = rows[i];
    int8_t j = i - 1;
    while (j >= 0 && rows[j].delta > key.delta) {
      rows[j + 1] = rows[j];
      j--;
    }
    rows[j + 1] = key;
  }

  // Rendering eventi
  for (uint8_t i = 0; i < n; i++) {

    CalItem& ev = cal[rows[i].idx];

    // Titolo evento (serve String per drawBoldMain → alloc temporanea)
    drawBoldMain(PAGE_X, y, String(ev.summary), TEXT_SCALE + 1);

    // Avvicina l’orario al titolo (vicinissimo, non sovrapposto)
    y += (CHAR_H * (TEXT_SCALE + 1)) - 6;

    // Orario
    gfx->setTextSize(2);
    gfx->setTextColor(COL_TEXT, COL_BG);
    gfx->setCursor(PAGE_X, y);
    gfx->print(ev.when);

    // Spazio dopo orario
    y += CHAR_H * 2 + 4;
    gfx->setTextSize(TEXT_SCALE);

    // Separatore tra eventi
    if (i < n - 1) {
      drawHLine(y);
      y += 12;
    }

    // Evita di uscire dal pannello
    if (y > 450)
      break;
  }
}

