/* ============================================================================
   PAGES MODULE – SquaredCoso
   Contiene TUTTE le pagine visualizzabili sul pannello.
   Versione SENZA pagina partenze.
============================================================================ */

/* ============================================================================
   PAGINA: QOD
============================================================================ */

static String qod_text = "";
static String qod_author = "";
// Cache "per giorno" per la frase del giorno
static String qod_date_ymd = "";  // es. "20251113"
static bool qod_from_ai = false;  // true = GPT, false = ZenQuotes

// Fallback ZenQuotes
static bool fetchQOD_ZenQuotes() {
  String body;
  if (!httpGET("https://zenquotes.io/api/today", body, 10000)) return false;
  String q, a;
  if (!jsonFindStringKV(body, "q", 0, q)) return false;
  jsonFindStringKV(body, "a", 0, a);
  qod_text = sanitizeText(q);
  qod_author = sanitizeText(a);
  if (qod_text.length() > 280) qod_text = qod_text.substring(0, 277) + "...";
  return true;
}

static bool fetchQOD_OpenAI() {
  if (!g_oa_key.length() || !g_oa_topic.length()) return false;

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setTimeout(10000);

  if (!http.begin(client, "https://api.openai.com/v1/responses")) return false;

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + g_oa_key);

  String prompt =
    "Scrivi una breve frase in italiano sull'argomento: " + g_oa_topic + ". Rispondi solo con la frase, senza virgolette, senza spiegazioni. Non includere il nome dell'argomento nella risposta.";

  // Modello: gpt-4.1-nano — reasoning disattivato
  String body =
    String("{\"model\":\"gpt-4.1-nano\","
           "\"max_output_tokens\":50,"
           "\"input\":\"")
    + jsonEscape(prompt) + "\"}";

  int code = http.POST(body);
  Serial.print("[QOD] OpenAI HTTP code: ");
  Serial.println(code);

  if (!isHttpOk(code)) {
    String err = http.getString();
    Serial.print("[QOD] OpenAI error: ");
    Serial.println(err);
    http.end();
    return false;
  }

  String resp = http.getString();
  http.end();

  //---------------------------------------------------------
  // Estrai il "text" dal blocco "output"
  //---------------------------------------------------------

  String text;

  // Cerca solo all’interno di "output"
  int outPos = indexOfCI(resp, "\"output\"");
  if (outPos < 0) outPos = 0;  // fallback

  // jsonFindStringKV con offset -> prende il primo "text" dopo outPos
  if (!jsonFindStringKV(resp, "text", outPos, text)) {
    Serial.println("[QOD] Nessun campo 'text' trovato:");
    Serial.println(resp.substring(0, 400));
    return false;
  }

  //---------------------------------------------------------
  // Pulizia del testo
  //---------------------------------------------------------

  String tmp = decodeJsonUnicode(text);
  tmp.replace("\n", " ");
  tmp.replace("\r", " ");
  tmp.trim();

  // solo ORA chiami sanitizeText:
  qod_text = sanitizeText(tmp);


  while (qod_text.startsWith("\"") || qod_text.startsWith("'") || qod_text.startsWith("“") || qod_text.startsWith("”"))
    qod_text.remove(0, 1), qod_text.trim();

  while (qod_text.endsWith("\"") || qod_text.endsWith("'") || qod_text.endsWith("“") || qod_text.endsWith("”"))
    qod_text.remove(qod_text.length() - 1), qod_text.trim();

  if (qod_text.length() > 280)
    qod_text = qod_text.substring(0, 277) + "...";

  qod_author = "AI Generated";
  return qod_text.length() > 0;
}

// Wrapper: gestisce cache per giorno + priorità OpenAI -> ZenQuotes
static bool fetchQOD() {
  String today;
  todayYMD(today);  // es "20251113"

  bool want_ai = g_oa_key.length() && g_oa_topic.length();

  // Se abbiamo già una frase per oggi, e la fonte corrisponde alla config attuale, usa cache
  if (qod_text.length() && qod_date_ymd == today) {
    if ((want_ai && qod_from_ai) || (!want_ai && !qod_from_ai)) {
      // stessa fonte desiderata, stessa data: non rifacciamo chiamate
      return true;
    }
    // altrimenti (es. oggi prima ZenQuotes, poi configuri OpenAI): facciamo una nuova fetch
  }

  // Azzeriamo sempre prima di un nuovo tentativo
  qod_text = "";
  qod_author = "";
  bool ok = false;

  // 1) Se configurata, prova OpenAI (gpt-5-nano)
  if (want_ai) {
    ok = fetchQOD_OpenAI();
    if (ok) {
      qod_date_ymd = today;
      qod_from_ai = true;
      return true;
    }
  }

  // 2) Fallback: ZenQuotes
  ok = fetchQOD_ZenQuotes();
  if (ok) {
    qod_date_ymd = today;
    qod_from_ai = false;
  }

  return ok;
}

static void handleForceQOD() {
  // forza nuova frase ignorando la cache
  qod_text = "";
  qod_author = "";
  qod_date_ymd = "";
  qod_from_ai = false;

  fetchQOD();
  drawCurrentPage();

  web.send(200, "text/html; charset=utf-8",
           "<!doctype html><meta charset='utf-8'><body>"
           "<h3>Frase rigenerata.</h3>"
           "<p><a href='/settings'>Torna indietro</a></p>"
           "</body>");
}



static void pageQOD() {

  drawHeader("Frase del giorno");
  int y = PAGE_Y;

  if (!qod_text.length()) {
    drawBoldMain(PAGE_X, y + CHAR_H,
                 g_lang == "it" ? "Nessuna frase disponibile" : "No quote available");
    return;
  }

  uint8_t scale = 2;
  if (qod_text.length() < 80) scale = 4;
  else if (qod_text.length() < 160) scale = 3;

  drawParagraph(PAGE_X, y, PAGE_W,
                String("“") + qod_text + "”", scale);

  String author =
    qod_author.length() ? ("- " + qod_author) : "— sconosciuto";

  uint8_t aScale = (author.length() < 18 ? 3 : 2);

  int authorY = PAGE_Y + PAGE_H - (BASE_CHAR_H * aScale) - 8;
  int authorW = author.length() * BASE_CHAR_W * aScale;
  int authorX = (480 - authorW) / 2;

  gfx->setTextColor(COL_ACCENT1, COL_BG);
  gfx->setTextSize(aScale);
  gfx->setCursor(authorX + 1, authorY);
  gfx->print(author);
  gfx->setCursor(authorX, authorY + 1);
  gfx->print(author);
  gfx->setCursor(authorX + 1, authorY + 1);
  gfx->print(author);
  gfx->setCursor(authorX, authorY);
  gfx->print(author);
  gfx->setTextSize(TEXT_SCALE);
}

/* ============================================================================
   PAGINA: METEO
============================================================================ */
// =========================== Meteo (wttr.in) ================================
static float w_now_tempC = NAN;
static String w_now_desc = "";
static float w_minC[3] = { NAN, NAN, NAN };
static float w_maxC[3] = { NAN, NAN, NAN };
static String w_desc[3] = { "", "", "" };

static bool jsonFindStringKV(const String& body, const String& key, int from, String& outVal) {
  String k = String("\"") + key + String("\"");
  int p = body.indexOf(k, from);
  if (p < 0) return false;
  p = body.indexOf(':', p);
  if (p < 0) return false;
  int q1 = body.indexOf('"', p + 1);
  if (q1 < 0) return false;
  int q2 = body.indexOf('"', q1 + 1);
  if (q2 < 0) return false;
  outVal = body.substring(q1 + 1, q2);
  return true;
}
static bool fetchWeather() {
  String cityURL = g_city;
  cityURL.trim();
  cityURL.replace(" ", "%20");
  String url = String("https://wttr.in/") + cityURL + "?format=j1&lang=" + g_lang;
  String body;
  if (!httpGET(url, body, 12000)) return false;

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
      else if (c == '}') {
        depth--;
        if (depth == 0) {
          endBrace = i;
          break;
        }
      }
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
static void pageWeather() {

  drawHeader("Meteo per " + sanitizeText(g_city));
  int y = PAGE_Y;

  String line1 =
    (!isnan(w_now_tempC) && w_now_desc.length())
      ? String((int)round(w_now_tempC)) + "C  |  " + w_now_desc
      : (g_lang == "it" ? "Dati non disponibili" : "Data not available");

  drawBoldMain(PAGE_X, y + CHAR_H, line1);
  y += CHAR_H * 2 + ITEMS_LINES_SP;

  drawHLine(y);
  y += 10;

  for (int i = 0; i < 3; i++) {

    String row =
      (g_lang == "it" ? "Giorno +" : "Day ") + String(i + 1) + ": ";

    if (!isnan(w_minC[i]) && !isnan(w_maxC[i]))
      row += String((int)round(w_minC[i])) + "-" + String((int)round(w_maxC[i])) + "C ->";
    else
      row += (g_lang == "it" ? "n/d" : "n/a");

    if (w_desc[i].length()) row += " " + w_desc[i];

    drawBoldMain(PAGE_X, y + CHAR_H, row);
    y += CHAR_H * 2 + 4;
  }
}
/* ============================================================================
   PAGINA: ARIA – COMPLETA E CORRETTA
   (Tutte le funzioni necessarie realmente fuori da qualsiasi altra funzione)
============================================================================ */

static float aq_pm25 = NAN, aq_pm10 = NAN, aq_o3 = NAN, aq_no2 = NAN;

/* ---- Estrae un oggetto JSON racchiuso tra { ... } ---- */
static bool extractObjectBlock(const String& body, const String& key, String& out) {
  int k = indexOfCI(body, String("\"") + key + String("\""));
  if (k < 0) return false;
  int b = body.indexOf('{', k);
  if (b < 0) return false;

  int depth = 0;
  for (int i = b; i < (int)body.length(); i++) {
    char c = body.charAt(i);
    if (c == '{') depth++;
    else if (c == '}') {
      depth--;
      if (depth == 0) {
        out = body.substring(b, i + 1);
        return true;
      }
    }
  }
  return false;
}

/* ---- Prende il PRIMO numero dentro un array JSON [ ... ] ---- */
static bool parseFirstNumberList(const String& obj, const String& key,
                                 float* out, int /*want*/) {

  int p = indexOfCI(obj, "\"" + key + "\"");
  if (p < 0) return false;

  int a = obj.indexOf('[', p);
  if (a < 0) return false;

  int s = a + 1;
  while (s < (int)obj.length() && (obj[s] == ' ' || obj[s] == '\n' || obj[s] == '\r' || obj[s] == '\t'))
    s++;

  if (s < (int)obj.length() && obj[s] == '"') return false;

  int e = s;
  while (e < (int)obj.length() && obj[e] != ',' && obj[e] != ']')
    e++;

  if (e <= s) return false;

  String num = obj.substring(s, e);
  num.trim();
  out[0] = num.toFloat();
  return true;
}

/* ---- Geocoding lato Open-Meteo (solo se non abbiamo lat/lon) ---- */
static bool geocodeIfNeeded() {
  if (g_lat.length() && g_lon.length()) return true;

  String url = "https://geocoding-api.open-meteo.com/v1/search?count=1&format=json&name="
               + g_city + "&language=" + g_lang;

  String body;
  if (!httpGET(url, body)) return false;
  int p = indexOfCI(body, "\"latitude\"");
  if (p < 0) return false;
  int c = body.indexOf(':', p);
  if (c < 0) return false;
  int e = body.indexOf(',', c + 1);
  if (e < 0) e = body.length();
  g_lat = sanitizeText(body.substring(c + 1, e));
  p = indexOfCI(body, "\"longitude\"");
  if (p < 0) return false;
  c = body.indexOf(':', p);
  if (c < 0) return false;
  e = body.indexOf(',', c + 1);
  if (e < 0) e = body.length();
  g_lon = sanitizeText(body.substring(c + 1, e));
  if (!g_lat.length() || !g_lon.length()) return false;

  saveAppConfig();
  return true;
}

/* ---- Categoria aria da valore ---- */
static int catFrom(float v, float a, float b, float c, float d) {
  if (isnan(v)) return -1;
  if (v <= a) return 0;
  if (v <= b) return 1;
  if (v <= c) return 2;
  if (v <= d) return 3;
  return 4;
}

/* ---- Etichetta categoria ---- */
static const char* catLabel(int c) {
  switch (c) {
    case 0: return "Buona, ";
    case 1: return "discreta, ";
    case 2: return "moderata, ";
    case 3: return "scadente, ";
    default: return "molto scadente, ";
  }
}

/* ---- Fetch qualità aria da Open-Meteo ---- */
static bool fetchAir() {
  if (!geocodeIfNeeded()) return false;

  String url =
    "https://air-quality-api.open-meteo.com/v1/air-quality?latitude=" + g_lat + "&longitude=" + g_lon + "&hourly=pm2_5,pm10,ozone,nitrogen_dioxide&timezone=auto";

  String body;
  if (!httpGET(url, body)) return false;

  String hourlyBlk;
  if (!extractObjectBlock(body, "hourly", hourlyBlk)) return false;

  float tmp[1];

  aq_pm25 = parseFirstNumberList(hourlyBlk, "pm2_5", tmp, 1) ? tmp[0] : NAN;
  aq_pm10 = parseFirstNumberList(hourlyBlk, "pm10", tmp, 1) ? tmp[0] : NAN;
  aq_o3 = parseFirstNumberList(hourlyBlk, "ozone", tmp, 1) ? tmp[0] : NAN;
  aq_no2 = parseFirstNumberList(hourlyBlk, "nitrogen_dioxide", tmp, 1) ? tmp[0] : NAN;

  return true;
}

/* ---- Testo finale “verdetto” qualità aria ---- */
static String airVerdict() {
  int worst = -1;

  int c = catFrom(aq_pm25, 10, 20, 25, 50);
  if (c > worst) worst = c;
  c = catFrom(aq_pm10, 20, 40, 50, 100);
  if (c > worst) worst = c;
  c = catFrom(aq_o3, 80, 120, 180, 240);
  if (c > worst) worst = c;
  c = catFrom(aq_no2, 40, 90, 120, 230);
  if (c > worst) worst = c;

  if (worst < 0) return "Aria: n/d";

  String msg = String("Aria ") + catLabel(worst);

  if (worst == 0) msg += "aria pulita";
  else if (worst == 1) msg += "nessun problema rilevante";
  else if (worst == 2) msg += "attenzione per soggetti sensibili";
  else if (worst == 3) msg += "limita attivita all'aperto se possibile";
  else msg += "evita attivita all'aperto";

  return msg;
}

/* ---- Pagina vera e propria ---- */
static void pageAir() {
  drawHeader("Aria a " + sanitizeText(g_city));
  int y = PAGE_Y;

  String r1 = "PM. 2.5  : " + (isnan(aq_pm25) ? "--" : String(aq_pm25, 1)) + " ug/m3";
  String r2 = "PM. 10   : " + (isnan(aq_pm10) ? "--" : String(aq_pm10, 1)) + " ug/m3";
  String r3 = "OZONO    : " + (isnan(aq_o3) ? "--" : String(aq_o3, 0)) + " ug/m3";
  String r4 = "B. AZOTO : " + (isnan(aq_no2) ? "--" : String(aq_no2, 0)) + " ug/m3";

  drawBoldMain(PAGE_X, y + CHAR_H, r1);
  y += CHAR_H * 2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, r2);
  y += CHAR_H * 2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, r3);
  y += CHAR_H * 2 + 4;
  drawBoldMain(PAGE_X, y + CHAR_H, r4);
  y += CHAR_H * 2 + 10;
  drawHLine(y);
  y += 10;

  String verdict = airVerdict();

  verdict.replace("aria pulita", "ottimo!");
  verdict.replace("nessun problema rilevante", "tutto ok.");
  verdict.replace("attenzione per soggetti sensibili", "moderata.");
  verdict.replace("limita attivita all'aperto se possibile", "è scadente...");
  verdict.replace("evita attivita all'aperto", "presta attenzione!");

  if (verdict.length() > 46)
    verdict = verdict.substring(0, 46) + "...";

  gfx->setTextSize(TEXT_SCALE);
  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setCursor(PAGE_X, y + CHAR_H);
  gfx->print(sanitizeText(verdict));
}


/* ============================================================================
   PAGINA: OROLOGIO
============================================================================ */
static void pageClock() {

  time_t now;
  struct tm t;

  time(&now);
  localtime_r(&now, &t);

  int minutes = t.tm_hour * 60 + t.tm_min;

  String greeting;

  if (minutes < 360) greeting = "Buonanotte!";
  else if (minutes < 690) greeting = "Buongiorno!";
  else if (minutes < 840) greeting = "Buon appetito!";
  else if (minutes < 1080) greeting = "Buon pomeriggio!";
  else if (minutes < 1320) greeting = "Buonasera!";
  else greeting = "Buonanotte!";

  drawHeader(greeting);

  char bufH[3], bufM[3], bufD[24];
  snprintf(bufH, 3, "%02d", t.tm_hour);
  snprintf(bufM, 3, "%02d", t.tm_min);
  snprintf(bufD, 24, "%02d/%02d/%04d",
           t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);

  const int timeScale = 16;
  const int dateScale = 5;
  const int chW = BASE_CHAR_W * timeScale;
  const int chH = BASE_CHAR_H * timeScale;

  const int timeTop = PAGE_Y + 28;
  const int timeX = (480 - 5 * chW) / 2;

  gfx->fillRect(PAGE_X, timeTop - 8,
                PAGE_W,
                chH + 36 + (BASE_CHAR_H * dateScale) + 12 + 16,
                COL_BG);

  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(timeScale);

  gfx->setCursor(timeX, timeTop);
  gfx->print(bufH);

  gfx->setCursor(timeX + 3 * chW, timeTop);
  gfx->print(bufM);

  {
    const int colonX = timeX + 2 * chW;
    const int colonY = timeTop;
    const int colonW = chW;

    gfx->fillRect(colonX, colonY, colonW, chH, COL_BG);

    int dotW = max(4, chW / 6);
    int dotH = max(4, chH / 10);
    int dotX = colonX + (colonW - dotW) / 2;

    int upperY = colonY + (chH * 0.32f);
    int lowerY = colonY + (chH * 0.68f) - dotH;

    gfx->fillRect(dotX, upperY, dotW, dotH, COL_ACCENT1);
    gfx->fillRect(dotX, lowerY, dotW, dotH, COL_ACCENT1);
  }

  int sepY = timeTop + chH + 12;
  drawHLine(sepY);

  gfx->setTextSize(dateScale);

  int dw = strlen(bufD) * BASE_CHAR_W * dateScale;

  gfx->setCursor((480 - dw) / 2, sepY + 22);
  gfx->print(bufD);

  gfx->setTextSize(TEXT_SCALE);
}

/* ============================================================================
   PAGINA: CALENDARIO
============================================================================ */
// ====================== Calendario ICS (solo OGGI) ==========================
struct CalItem {
  String when;
  String summary;
  String where;
  time_t ts;
  bool allDay;
};
CalItem cal[3];

static String trimField(const String& s) {
  String t = s;
  t.trim();
  return t;
}
static void resetCal() {
  for (int i = 0; i < 3; i++) {
    cal[i].when = "";
    cal[i].summary = "";
    cal[i].where = "";
    cal[i].ts = 0;
    cal[i].allDay = false;
  }
}
static String extractAfterColon(const String& src, int pos) {
  int c = src.indexOf(':', pos);
  if (c < 0) return "";
  int e = src.indexOf('\n', c + 1);
  if (e < 0) e = src.length();
  return trimField(src.substring(c + 1, e));
}
static bool isTodayStamp(const String& dtstampYmd, const String& todayYmd) {
  if (dtstampYmd.length() < 8) return false;
  return dtstampYmd.substring(0, 8) == todayYmd;
}
static void humanTimeFromStamp(const String& stamp, String& out) {
  if (stamp.length() >= 15 && stamp.charAt(8) == 'T') {
    String hh = stamp.substring(9, 11);
    String mm = stamp.substring(11, 13);
    out = hh + ":" + mm;
  } else out = "tutto il giorno";
}
static bool fetchICS() {
  resetCal();
  if (!g_ics.length()) return true;

  String body;
  if (!httpGET(g_ics, body, 15000)) return false;

  String today;
  todayYMD(today);

  int idx = 0;
  int p = 0;
  while (idx < 3) {
    int b = body.indexOf("BEGIN:VEVENT", p);
    if (b < 0) break;
    int e = body.indexOf("END:VEVENT", b);
    if (e < 0) break;
    String blk = body.substring(b, e);

    int ds = indexOfCI(blk, "DTSTART");
    String rawStart = "";
    if (ds >= 0) rawStart = extractAfterColon(blk, ds);
    if (!rawStart.length()) {
      p = e + 10;
      continue;
    }

    String ymd = rawStart.substring(0, 8);
    if (!isTodayStamp(ymd, today)) {
      p = e + 10;
      continue;
    }

    int ss = indexOfCI(blk, "SUMMARY");
    String summary = "";
    if (ss >= 0) summary = extractAfterColon(blk, ss);
    int ls = indexOfCI(blk, "LOCATION");
    String where = "";
    if (ls >= 0) where = extractAfterColon(blk, ls);

    String when;
    humanTimeFromStamp(rawStart, when);

    struct tm tt = {};
    tt.tm_year = rawStart.substring(0, 4).toInt() - 1900;
    tt.tm_mon = rawStart.substring(4, 6).toInt() - 1;
    tt.tm_mday = rawStart.substring(6, 8).toInt();
    bool hasTime = (rawStart.length() >= 15 && rawStart.charAt(8) == 'T');
    if (hasTime) {
      tt.tm_hour = rawStart.substring(9, 11).toInt();
      tt.tm_min = rawStart.substring(11, 13).toInt();
      tt.tm_sec = 0;
    } else {
      tt.tm_hour = 0;
      tt.tm_min = 0;
      tt.tm_sec = 0;
    }
    time_t evt_ts = mktime(&tt);

    if (summary.length()) {
      cal[idx].when = sanitizeText(when);
      cal[idx].summary = sanitizeText(summary);
      cal[idx].where = sanitizeText(where);
      cal[idx].ts = evt_ts;
      cal[idx].allDay = !hasTime;
      idx++;
    }
    p = e + 10;
  }
  return true;
}

static void pageCalendar() {

  drawHeader("Calendario (oggi)");
  int y = PAGE_Y;

  struct Row {
    String when, summary, where;
    time_t ts;
    bool allDay;
    long delta;
  } rows[3];

  int n = 0;

  time_t now;
  time(&now);

  for (int i = 0; i < 3; i++) {
    if (!cal[i].summary.length()) continue;

    long d;

    if (cal[i].allDay)
      d = 0;
    else {
      d = difftime(cal[i].ts, now);
      if (d < 0) d = 24L * 3600L;
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
        Row tmp = rows[i];
        rows[i] = rows[j];
        rows[j] = tmp;
      }
    }
  }

  if (n == 0) {
    drawBoldMain(PAGE_X, y + CHAR_H,
                 g_lang == "it" ? "Nessun evento" : "No events today");
    return;
  }

  for (int i = 0; i < n; i++) {

    drawBoldMain(PAGE_X, y + CHAR_H, rows[i].summary);
    y += CHAR_H * 2;

    if (rows[i].when.length()) {
      gfx->setTextSize(1);
      gfx->setTextColor(COL_TEXT, COL_BG);
      gfx->setCursor(PAGE_X, y);
      gfx->print(rows[i].when);
      y += 16;
    }

    if (rows[i].where.length()) {
      gfx->setTextColor(COL_TEXT, COL_BG);
      gfx->setTextSize(1);
      gfx->setCursor(PAGE_X, y);
      gfx->print(rows[i].where);
      y += 16;
    }

    gfx->setTextSize(TEXT_SCALE);
    y += 6;

    if (i < n - 1) {
      drawHLine(y);
      y += 10;
    }

    if (y > 460) break;
  }
}

/* ============================================================================
   PAGINA: BITCOIN
============================================================================ */
static double btc_chf = NAN;
static uint32_t btc_last_ok_ms = 0;

static bool jsonFindNumberKV(const String& body, const String& key, int from, double& outVal) {
  String k = String("\"") + key + String("\"");
  int p = body.indexOf(k, from);
  if (p < 0) return false;
  p = body.indexOf(':', p);
  if (p < 0) return false;
  int s = p + 1;
  while (s < (int)body.length() && (body[s] == ' ' || body[s] == '\n' || body[s] == '\r' || body[s] == '\t')) s++;
  int e = s;
  while (e < (int)body.length()) {
    char c = body[e];
    if (c == ',' || c == '}' || c == ']' || c == ' ') break;
    e++;
  }
  if (e <= s) return false;
  String num = body.substring(s, e);
  num.trim();
  outVal = num.toDouble();
  return true;
}

static String formatCH(float v) {
  if (isnan(v)) return String("--");
  String s = String(v, 8);
  s.replace('.', ',');
  return s;
}


static String formatCHF(double v) {
  if (isnan(v)) return String("--.--");
  long long intPart = (long long)llround(floor(v));
  int cents = (int)llround((v - floor(v)) * 100.0);
  if (cents == 100) {
    intPart += 1;
    cents = 0;
  }
  String sInt = String(intPart);
  String out = "";
  int count = 0;
  for (int i = sInt.length() - 1; i >= 0; --i) {
    out = sInt.charAt(i) + out;
    count++;
    if (count == 3 && i > 0) {
      out = String("'") + out;
      count = 0;
    }
  }
  if (cents == 0) return out;
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d", cents);
  return out + String(".") + String(buf);
}
static bool fetchBTC() {
  String url = "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=chf";
  String body;
  if (!httpGET(url, body, 10000)) return false;
  int b = indexOfCI(body, "\"bitcoin\"");
  if (b < 0) return false;
  double val = NAN;
  if (!jsonFindNumberKV(body, "chf", b, val)) return false;
  btc_chf = val;
  btc_last_ok_ms = millis();
  return true;
}

static void pageBTC() {

  drawHeader("Valore Bitcoin");
  int y = PAGE_Y;

  String price =
    isnan(btc_chf) ? "--.--" : formatCHF(btc_chf);

  gfx->setTextColor(COL_TEXT, COL_BG);
  gfx->setTextSize(6);

  String line = "CHF " + price;
  int tw = line.length() * BASE_CHAR_W * 6;

  gfx->setCursor((480 - tw) / 2, y + 80);
  gfx->print(line);

  if (!isnan(g_btc_owned) && !isnan(btc_chf)) {

    double chf_owned = g_btc_owned * btc_chf;

    gfx->setTextSize(3);

    String ownedLine = formatCH(g_btc_owned) + " BTC";
    int tw2 = ownedLine.length() * BASE_CHAR_W * 3;

    gfx->setCursor((480 - tw2) / 2, y + 160);
    gfx->print(ownedLine);

    String ownedCHF = "= CHF " + formatCHF(chf_owned);
    int tw3 = ownedCHF.length() * BASE_CHAR_W * 3;

    gfx->setCursor((480 - tw3) / 2, y + 200);
    gfx->print(ownedCHF);
  }

  gfx->setTextSize(2);
  y += 240;

  drawHLine(y);
  y += 12;

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
// countdown
/* ============================================================================
   PAGINA: COUNTDOWN
============================================================================ */

static bool parseISOToTimeT(const String& iso, time_t& out) {
  if (!iso.length() || iso.length() < 16) return false;
  struct tm t = {};
  t.tm_year = iso.substring(0, 4).toInt() - 1900;
  t.tm_mon = iso.substring(5, 7).toInt() - 1;
  t.tm_mday = iso.substring(8, 10).toInt();
  t.tm_hour = iso.substring(11, 13).toInt();
  t.tm_min = iso.substring(14, 16).toInt();
  t.tm_sec = 0;
  out = mktime(&t);
  return out > 0;
}

static String formatDelta(time_t target) {
  time_t now;
  time(&now);
  long diff = (long)difftime(target, now);
  if (diff <= 0) return "scaduto";
  long d = diff / 86400L;
  diff %= 86400L;
  long h = diff / 3600L;
  diff %= 3600L;
  long m = diff / 60L;

  char buf[64];
  if (d > 0)
    snprintf(buf, sizeof(buf), "%ldg %02ldh %02ldm", d, h, m);
  else
    snprintf(buf, sizeof(buf), "%02ldh %02ldm", h, m);

  return String(buf);
}

static void pageCountdowns() {

  drawHeader("Countdown");
  int y = PAGE_Y;

  struct Row {
    String name;
    time_t when;
  } list[8];

  int n = 0;

  for (int i = 0; i < 8; i++) {
    if (!cd[i].name.length() || !cd[i].whenISO.length()) continue;
    time_t t;
    if (!parseISOToTimeT(cd[i].whenISO, t)) continue;
    list[n].name = cd[i].name;
    list[n].when = t;
    n++;
  }

  if (n == 0) {
    drawBoldMain(PAGE_X, y + CHAR_H, "Nessun countdown");
    return;
  }

  // Ordina cronologicamente
  for (int i = 0; i < n - 1; i++) {
    for (int j = i + 1; j < n; j++) {
      if (difftime(list[i].when, list[j].when) > 0) {
        Row tmp = list[i];
        list[i] = list[j];
        list[j] = tmp;
      }
    }
  }

  // Disegno righe
  for (int i = 0; i < n; i++) {

    String row = sanitizeText(list[i].name) + "  (" + formatShortDate(list[i].when) + ")  " + formatDelta(list[i].when);

    drawBoldMain(PAGE_X, y + CHAR_H, row);
    y += CHAR_H * 2 + 8;

    if (i < n - 1) {
      drawHLine(y);
      y += 10;
    }

    if (y > 460) break;
  }
}

/* ============================================================================
   PAGINA: VALUTE CHF (Exchangerate.host)
============================================================================ */
static double fx_eur = NAN, fx_usd = NAN, fx_gbp = NAN, fx_jpy = NAN;

static bool fetchFX() {
  String body;
  String url = "https://api.frankfurter.app/latest?from=CHF&to=EUR,USD,GBP,JPY";

  if (!httpGET(url, body, 10000)) return false;

  auto getRate = [&](const char* code) -> double {
    String key = String("\"") + code + "\":";
    int p = body.indexOf(key);
    if (p < 0) return NAN;

    int s = p + key.length();
    int e = s;
    while (e < body.length() && (isdigit(body[e]) || body[e] == '.' || body[e] == '-'))
      e++;

    return body.substring(s, e).toDouble();
  };

  fx_eur = getRate("EUR");
  fx_usd = getRate("USD");
  fx_gbp = getRate("GBP");
  fx_jpy = getRate("JPY");

  return true;
}

static void pageFX() {
  drawHeader("Valute (CHF Fast)");
  int y = PAGE_Y;

  if (isnan(fx_eur) && isnan(fx_usd) && isnan(fx_gbp) && isnan(fx_jpy)) {
    drawBoldMain(PAGE_X, y + CHAR_H, "Dati non disponibili");
    return;
  }

  drawBoldMain(PAGE_X, y + CHAR_H, "EUR: " + String(fx_eur, 3));
  y += CHAR_H * 2 + 4;

  drawBoldMain(PAGE_X, y + CHAR_H, "USD: " + String(fx_usd, 3));
  y += CHAR_H * 2 + 4;

  drawBoldMain(PAGE_X, y + CHAR_H, "GBP: " + String(fx_gbp, 3));
  y += CHAR_H * 2 + 4;

  drawBoldMain(PAGE_X, y + CHAR_H, "JPY: " + String(fx_jpy, 1));
  y += CHAR_H * 2 + 4;
}



/* ============================================================================
   PAGINA: TEMPERATURA 
============================================================================ */
// Trova la parentesi ']' che chiude l'array JSON aperto a 'start'
static int findMatchingBracket(const String& src, int start) {
  if (start < 0 || start >= src.length() || src[start] != '[') return -1;

  int depth = 0;
  for (int i = start; i < (int)src.length(); i++) {
    char c = src[i];
    if (c == '[') {
      depth++;
    } else if (c == ']') {
      depth--;
      if (depth == 0) {
        return i;  // indice della ']' corrispondente
      }
    }
  }
  return -1;  // non trovata
}


static float t24[24];

static bool fetchTemp24() {

  // --- reset array ---
  for (int i = 0; i < 24; i++)
    t24[i] = NAN;

  // --- serve lat/lon ---
  if (!geocodeIfNeeded()) return false;

  // 7 giorni, temperatura media giornaliera
  String url =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude="
    + g_lat + "&longitude=" + g_lon + "&daily=temperature_2m_mean"
                                      "&forecast_days=7&timezone=auto";

  String body;
  if (!httpGET(url, body, 12000)) return false;

  // --------------------------------------------------------------------
  // Estrai l'array daily.temperature_2m_mean
  // --------------------------------------------------------------------

  int posDaily = indexOfCI(body, "\"daily\"");
  if (posDaily < 0) return false;

  int posMean = indexOfCI(body, "\"temperature_2m_mean\"", posDaily);
  if (posMean < 0) return false;

  int lb = body.indexOf('[', posMean);
  if (lb < 0) return false;

  int rb = findMatchingBracket(body, lb);
  if (rb < 0) return false;

  String arr = body.substring(lb + 1, rb);

  // I 7 valori successivi
  float seven[7];
  for (int i = 0; i < 7; i++) seven[i] = NAN;

  int idx = 0;
  int start = 0;

  while (idx < 7 && start < arr.length()) {
    // cerca un numero (float possibile con decimali)
    int s = start;
    while (s < (int)arr.length() && !((arr[s] >= '0' && arr[s] <= '9') || arr[s] == '-'))
      s++;

    if (s >= (int)arr.length()) break;

    int e = s;
    while (e < (int)arr.length() && ((arr[e] >= '0' && arr[e] <= '9') || arr[e] == '.' || arr[e] == '-'))
      e++;

    String num = arr.substring(s, e);
    seven[idx] = num.toFloat();
    idx++;

    start = e + 1;
  }

  if (idx == 0) return false;

  // --------------------------------------------------------------------
  // Redistribuiamo 7 valori dentro t24[] → 24 slot
  // --------------------------------------------------------------------

  // Posizioniamo i 7 valori ogni ~4 passi
  int anchors[7];
  for (int i = 0; i < 7; i++) {
    anchors[i] = (int)round(i * (24.0f / 6.0f));  // 0,4,8,12,16,20,24
    if (anchors[i] > 23) anchors[i] = 23;
  }

  // Imposta i punti base
  for (int i = 0; i < 7; i++) {
    t24[anchors[i]] = seven[i];
  }

  // Interpola fra ogni coppia di punti noti
  for (int a = 0; a < 6; a++) {
    int x1 = anchors[a];
    int x2 = anchors[a + 1];
    float y1 = seven[a];
    float y2 = seven[a + 1];

    int dx = x2 - x1;
    if (dx < 1) continue;

    float dy = (y2 - y1) / dx;

    for (int k = 1; k < dx; k++) {
      t24[x1 + k] = y1 + dy * k;
    }
  }

  return true;
}

static void pageTemp24() {
  drawHeader("Temp.prossimi giorni");

  float mn = 9999, mx = -9999;
  for (int i = 0; i < 24; i++) {
    if (!isnan(t24[i])) {
      mn = min(mn, t24[i]);
      mx = max(mx, t24[i]);
    }
  }

  if (mn > mx || mn == 9999) {
    drawBoldMain(PAGE_X, PAGE_Y + CHAR_H, "Dati non disponibili");
    return;
  }

  int X = 20, W = 440;
  int Y = PAGE_Y + 40, H = 260;

  gfx->drawRect(X, Y, W, H, COL_ACCENT2);

  float range = mx - mn;
  if (range < 0.1f) range = 0.1f;

  for (int i = 0; i < 23; i++) {
    if (isnan(t24[i]) || isnan(t24[i + 1])) continue;

    int x1 = X + (i * W) / 23;
    int x2 = X + ((i + 1) * W) / 23;

    int y1 = Y + H - (int)(((t24[i] - mn) / range) * H);
    int y2 = Y + H - (int)(((t24[i + 1] - mn) / range) * H);

    gfx->drawLine(x1, y1, x2, y2, COL_ACCENT1);
  }

  drawBoldMain(PAGE_X, Y + H + 20,
               String((int)mn) + "C - " + String((int)mx) + "C");
}

/* ============================================================================
   PAGINA: ORE DI LUCE (Sunrise-Sunset)
============================================================================ */

static String sun_alba = "";
static String sun_tram = "";
static String sun_durata = "";

static bool fetchSun() {
  sun_alba = "";
  sun_tram = "";
  sun_durata = "";

  if (!g_lat.length() || !g_lon.length()) return false;

  String url =
    "https://api.sunrise-sunset.org/json?lat=" + g_lat + "&lng=" + g_lon + "&formatted=0";

  String body;
  if (!httpGET(url, body, 10000)) return false;

  String alba, tram;
  if (!jsonFindStringKV(body, "sunrise", 0, alba)) return false;
  if (!jsonFindStringKV(body, "sunset", 0, tram)) return false;

  auto isoHM = [&](const String& iso) -> String {
    int t = iso.indexOf('T');
    if (t < 0 || t + 5 >= iso.length()) return String("--:--");
    return iso.substring(t + 1, t + 6);  // HH:MM
  };

  sun_alba = isoHM(alba);
  sun_tram = isoHM(tram);

  if (sun_alba.length() != 5 || sun_tram.length() != 5) return true;  // abbiamo almeno qualcosa

  int h1 = sun_alba.substring(0, 2).toInt();
  int m1 = sun_alba.substring(3, 5).toInt();
  int h2 = sun_tram.substring(0, 2).toInt();
  int m2 = sun_tram.substring(3, 5).toInt();

  int dmin = (h2 * 60 + m2) - (h1 * 60 + m1);
  if (dmin < 0) dmin += 24 * 60;

  char buf[16];
  snprintf(buf, sizeof(buf), "%02dh %02dm", dmin / 60, dmin % 60);
  sun_durata = buf;

  return true;
}

static void pageSun() {
  drawHeader("Ore di luce oggi");
  int y = PAGE_Y;

  if (!sun_alba.length() || !sun_tram.length()) {
    drawBoldMain(PAGE_X, y + CHAR_H, "Nessun dato disponibile");
    return;
  }

  drawBoldMain(PAGE_X, y + CHAR_H, "Alba:     " + sun_alba);
  y += CHAR_H * 2 + 8;

  drawBoldMain(PAGE_X, y + CHAR_H, "Tramonto: " + sun_tram);
  y += CHAR_H * 2 + 8;

  if (sun_durata.length()) {
    drawBoldMain(PAGE_X, y + CHAR_H, "Luce:     " + sun_durata);
  }
}
