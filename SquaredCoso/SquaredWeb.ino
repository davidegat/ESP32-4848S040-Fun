/* ============================================================================
   CAPTIVE PORTAL & WEBUI – versione ottimizzata RAM/CPU
   Scopo:
     - Gestire captive portal in modalità AP
     - Gestire la WebUI in modalità STA
     - Esporre pagine HTML leggere e responsive
     - Consentire configurazione Wi-Fi, pagine visibili, QOD, BTC, countdown
     - Gestire export/import config
   Note:
     - Tutto è pensato per minimizzare RAM e copie di stringhe.
     - HTML viene costruito on-demand, nessun buffer grande persistente.
============================================================================ */


/* ============================================================================
   HTTP helpers – funzioni leggere per GET e parsing minimale
============================================================================ */

static inline bool isHttpOk(int code) {
  return code >= 200 && code < 300;
}

// IMPLEMENTAZIONE CORRETTA: niente static, niente default args
bool httpGET(const String& url, String& out, uint32_t timeout) {
  HTTPClient http;
  http.setTimeout(timeout);

  if (!http.begin(url))
    return false;

  int code = http.GET();
  if (!isHttpOk(code)) {
    http.end();
    return false;
  }

  out = http.getString();
  http.end();
  return true;
}

// IMPLEMENTAZIONE CORRETTA: niente static, niente default args
int indexOfCI(const String& s, const String& pat, int from) {
  int n = s.length();
  int m = pat.length();
  if (m <= 0 || from < 0 || from >= n) return -1;

  for (int i = from; i <= n - m; ++i) {
    int j = 0;
    for (; j < m; ++j) {
      char c1 = s[i + j];
      char c2 = pat[j];

      if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
      if (c2 >= 'A' && c2 <= 'Z') c2 += 32;

      if (c1 != c2) break;
    }
    if (j == m) return i;
  }
  return -1;
}



/* ============================================================================
   CAPTIVE PORTAL (modalità AP)
   - handleRootAP(): pagina iniziale per configurazione WiFi
   - handleSave(): salva credenziali e riavvia
   - handleReboot(): reboot diretto
   - htmlAP(): pagina HTML leggera del portale captive
============================================================================ */

static void handleRootAP() {
  web.send(200, "text/html; charset=utf-8", htmlAP());
}

/*
   handleSave()
   Salva SSID/password in NVS (“wifi”), poi avvia il reboot automatico.
*/
static void handleSave() {
  if (web.hasArg("ssid") && web.hasArg("pass")) {

    prefs.begin("wifi", false);
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    prefs.end();

    // Html con reboot automatico
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

/*
   htmlAP()
   Pagina di configurazione WiFi mostrata quando l’ESP è in AP mode
   (captive portal). Semplice e senza script pesanti.
*/
static String htmlAP() {
  String ip = WiFi.softAPIP().toString();

  String page;
  page.reserve(512);  // nessuna frammentazione

  page += "<!doctype html><html><head>"
          "<meta charset='utf-8'/>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
          "<title>Wi-Fi Setup</title>"
          "<style>"
          "body{font-family:system-ui,Segoe UI,Roboto,Ubuntu,Arial,sans-serif}"
          "input{width:280px}label{display:block;margin-top:10px}"
          "</style></head><body>"
          "<h2>Configura Wi-Fi</h2>"
          "<form method='POST' action='/save'>"
          "<label>SSID</label><input name='ssid'/>"
          "<label>Password</label><input name='pass' type='password'/>"
          "<p><button type='submit'>Salva & Connetti</button></p>"
          "</form>"
          "<p>Se il popup non compare, apri <b>http://";
  page += ip;
  page += "/</b></p></body></html>";

  return page;
}


/* ============================================================================
   WEBUI – SEZIONE SETTINGS
   - checkbox()
   - htmlSettings(): pagina completa con linguaggio bilingue
   - htmlHome(): panoramica rapida
   - handler root per STA
============================================================================ */

// Genera checkbox singola per “Pagine visibili”
static String checkbox(const char* name, bool checked, const char* label) {
  String s;
  s.reserve(96);
  s += "<label class='chk'><input type='checkbox' name='";
  s += name;
  s += "' value='1' ";
  if (checked) s += "checked";
  s += "/><span>";
  s += label;
  s += "</span></label>";
  return s;
}

/*
   htmlSettings()
   Pagina principale di configurazione del device:
     - bilingue IT/EN
     - responsive
     - include tutte le sezioni: Generale, QOD, BTC, RSS, Pagine, Countdown, Backup
   Nota:
     - Tutto generato dinamicamente senza cache e senza template grossi.
     - È lungo ma leggerissimo per l’ESP (solo concatenazioni di String).
*/
String htmlSettings(bool saved, const String& msg) {

  const bool it = (g_lang == "it");

  /* ------------------------------------------------------------
     TESTI BILINGUE – centralizzati per ridurre logica interna
  ------------------------------------------------------------ */
  const char* t_title   = it ? "Impostazioni" : "Settings";
  const char* t_saved   = it ? "Impostazioni salvate. Ricarico…" :
                               "Settings saved. Reloading…";

  const char* t_general = it ? "Generale" : "General";
  const char* t_city    = it ? "Città" : "City";
  const char* t_lang    = it ? "Lingua (it/en)" : "Language (it/en)";
  const char* t_ics     = it ? "URL Calendario ICS" : "ICS Calendar URL";
  const char* t_pageint = it ? "Tempo cambio pagina (secondi)" :
                               "Page switch interval (seconds)";

  const char* t_qod      = it ? "Frase del giorno" : "Quote of the Day";
  const char* t_qoddesc  = it ? "Se imposti la chiave OpenAI userò GPT. Altrimenti ZenQuotes."
                              : "If you set the OpenAI key, GPT will be used. Otherwise ZenQuotes.";
  const char* t_oa_key   = it ? "Chiave API OpenAI" : "OpenAI API Key";
  const char* t_oa_topic = it ? "Argomento frase" : "Quote topic";
  const char* t_force    = it ? "Richiedi nuova frase" : "Get new quote";

  const char* t_btc      = "Bitcoin";
  const char* t_btc_amt  = it ? "Quantità BTC posseduti" : "Owned BTC amount";

  // RSS
  const char* t_rss       = it ? "Notizie RSS" : "RSS News";
  const char* t_rss_label = it ? "URL feed RSS" : "RSS feed URL";
  const char* t_rss_hint  = it ? "Lascia vuoto per usare il feed predefinito."
                               : "Leave empty to use the default feed.";

  const char* t_pages    = it ? "Pagine visibili" : "Visible pages";
  const char* t_hint2    = it ? "Se le disattivi tutte, l'orologio resta comunque attivo."
                              : "If you disable all, the clock will still remain active.";

  const char* t_count    = it ? "Countdown (max 8)" : "Countdowns (max 8)";
  const char* t_name     = it ? "Nome #" : "Name #";
  const char* t_time     = it ? "Data/Ora #" : "Date/Time #";

  const char* t_savebtn  = it ? "Salva" : "Save";
  const char* t_home     = "Home";

  const char* t_backup   = it ? "Backup" : "Backup";
  const char* t_export   = it ? "Esporta impostazioni" : "Export settings";
  const char* t_import   = it ? "Importa" : "Import";
  const char* t_load     = it ? "Carica" : "Load";

  /* ------------------------------------------------------------
     NOTICE (feedback salvataggio o messaggi di errore)
  ------------------------------------------------------------ */
  String notice;
  if (saved) {
    notice.reserve(64);
    notice = "<div class='alert ok'>";
    notice += t_saved;
    notice += "</div>";
  } else if (msg.length()) {
    notice = "<div class='alert warn'>";
    notice += msg;
    notice += "</div>";
  }

  /* ------------------------------------------------------------
     Conversioni veloci
  ------------------------------------------------------------ */
  uint32_t page_s = PAGE_INTERVAL_MS / 1000;

  /* ------------------------------------------------------------
     HTML PRINCIPALE (≈4 KB, generato on-demand)
  ------------------------------------------------------------ */
  String page;
  page.reserve(4096);

  /* ------------------------------------------------------------
     HEADER + AVVIO <form>
  ------------------------------------------------------------ */
  page += "<!doctype html><html><head>"
          "<meta charset='utf-8'/>"
          "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
          "<title>";
  page += t_title;
  page += "</title>"

          /* CSS leggero, dark mode */
          "<style>"
          "body{margin:0;font-family:system-ui,Segoe UI,Roboto;background:#0b0b0b;"
          "color:#f2f2f2;line-height:1.45;}"
          "header{position:sticky;top:0;background:#0c2250;padding:18px 20px;"
          "border-bottom:1px solid #1d1d1d;color:#ffdf40;font-size:1.4rem;font-weight:600;}"
          "main{max-width:900px;margin:0 auto;padding:20px;}"
          ".card{background:#111;border:1px solid #222;border-radius:14px;padding:20px;"
          "margin-bottom:20px;box-shadow:0 4px 18px #00000055;}"
          ".card h3{margin-top:0;color:#ffdf40;font-weight:600;font-size:1.15rem;}"
          "label{display:block;margin-top:14px;margin-bottom:4px;color:#ddd;font-size:.9rem;}"
          "input, select{width:100%;padding:10px 12px;border-radius:8px;border:1px solid #2d2d2d;"
          "background:#0f0f0f;color:#eee;transition:border .2s, box-shadow .2s;}"
          "input:focus{border-color:#0dad4a;box-shadow:0 0 0 3px #0dad4a33;}"
          ".row{display:flex;flex-wrap:wrap;gap:16px;}"
          ".row > div{flex:1;min-width:240px;}"
          ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:10px;}"
          ".chk{display:flex;align-items:center;gap:10px;padding:10px 12px;background:#151515;"
          "border:1px solid #292929;border-radius:10px;}"
          ".chk span{font-size:.9rem;}"
          ".btn{padding:10px 16px;border:none;border-radius:10px;cursor:pointer;font-weight:600;"
          "font-size:.95rem;}"
          ".primary{background:#0dad4a;color:#111;}"
          ".ghost{background:transparent;border:1px solid #333;color:#ddd;}"
          ".btn:hover{opacity:.9;}"
          ".alert{padding:12px 15px;border-radius:10px;margin-bottom:18px;}"
          ".ok{background:#10381e;border-left:4px solid #0dad4a;color:#c9ffd9;}"
          ".warn{background:#3a250c;border-left:4px solid #ff9e2c;color:#ffe4c4;}"
          "</style></head><body>"

          "<header>";
  page += t_title;
  page += "</header><main>";
  page += notice;

  page += "<form method='POST' action='/settings'>";

  /* ------------------------------------------------------------
     SEZIONE: GENERALE
  ------------------------------------------------------------ */
  page += "<div class='card'><h3>" + String(t_general) + "</h3>"
          "<div class='row'>"

          "<div><label>" + String(t_city) + "</label>"
          "<input name='city' value='" + sanitizeText(g_city) + "'/></div>"

          "<div><label>" + String(t_lang) + "</label>"
          "<input name='lang' value='" + sanitizeText(g_lang) + "'/></div>"
          "</div>"

          "<label>" + String(t_ics) + "</label>"
          "<input name='ics' value='" + sanitizeText(g_ics) + "'/>"

          "<label>" + String(t_pageint) + "</label>"
          "<input name='page_s' type='number' min='5' max='600' value='" +
          String(page_s) + "'/>"

          // Valuta base
          "<label>Valuta base:</label>"
          "<select name='fiat'>"
          "<option value='CHF' " + String(g_fiat == "CHF" ? "selected" : "") + ">CHF</option>"
          "<option value='EUR' " + String(g_fiat == "EUR" ? "selected" : "") + ">EUR</option>"
          "<option value='USD' " + String(g_fiat == "USD" ? "selected" : "") + ">USD</option>"
          "<option value='GBP' " + String(g_fiat == "GBP" ? "selected" : "") + ">GBP</option>"
          "</select>"

          "</div>";

  /* ------------------------------------------------------------
     SEZIONE: QOD
  ------------------------------------------------------------ */
  page += "<div class='card'><h3>" + String(t_qod) + "</h3>"
          "<p style='opacity:.7;font-size:.9rem'>" + String(t_qoddesc) + "</p>"

          "<label>" + String(t_oa_key) + "</label>"
          "<input name='openai_key' type='password' value='" +
          sanitizeText(g_oa_key) + "'/>"

          "<label>" + String(t_oa_topic) + "</label>"
          "<input name='openai_topic' value='" +
          sanitizeText(g_oa_topic) + "'/>"

          "<p style='margin-top:14px'>"
          "<button class='btn ghost' formaction='/force_qod' formmethod='POST'>" +
          String(t_force) + "</button></p>"
          "</div>";

  /* ------------------------------------------------------------
     SEZIONE: BITCOIN
  ------------------------------------------------------------ */
  page += "<div class='card'><h3>" + String(t_btc) + "</h3>"
          "<label>" + String(t_btc_amt) + "</label>"
          "<input name='btc_owned' type='number' step='0.00000001' value='";

  if (!isnan(g_btc_owned))
    page += String(g_btc_owned, 8);

  page += "'/></div>";

  /* ------------------------------------------------------------
     SEZIONE: RSS NEWS
  ------------------------------------------------------------ */
  page += "<div class='card'><h3>" + String(t_rss) + "</h3>"
          "<p style='opacity:.7;font-size:.9rem'>" + String(t_rss_hint) + "</p>"
          "<label>" + String(t_rss_label) + "</label>"
          "<input name='rss_url' value='" + sanitizeText(g_rss_url) + "'/></div>";

  /* ------------------------------------------------------------
     SEZIONE: PAGINE VISIBILI (griglia di checkbox)
  ------------------------------------------------------------ */
  page += "<div class='card'><h3>" + String(t_pages) + "</h3><div class='grid'>";

  page += checkbox("p_WEATHER", g_show[P_WEATHER], it ? "Meteo" : "Weather");
  page += checkbox("p_AIR",     g_show[P_AIR],     it ? "Qualità aria" : "Air quality");
  page += checkbox("p_CLOCK",   g_show[P_CLOCK],   it ? "Orologio" : "Clock");
  page += checkbox("p_CAL",     g_show[P_CAL],     it ? "Calendario ICS" : "ICS Calendar");
  page += checkbox("p_BTC",     g_show[P_BTC],     "Bitcoin");
  page += checkbox("p_QOD",     g_show[P_QOD],     it ? "Frase del giorno" : "Quote of the Day");
  page += checkbox("p_INFO",    g_show[P_INFO],    it ? "Info dispositivo" : "Device info");
  page += checkbox("p_COUNT",   g_show[P_COUNT],   "Countdown");
  page += checkbox("p_FX",      g_show[P_FX],      it ? "Valute CHF" : "CHF Currency");
  page += checkbox("p_T24",     g_show[P_T24],     it ? "Temperatura 24h" : "24h Temperature");
  page += checkbox("p_SUN",     g_show[P_SUN],     it ? "Ore di luce" : "Sunlight hours");
  page += checkbox("p_NEWS",    g_show[P_NEWS],    it ? "Notizie RSS" : "RSS News");

  page += "</div>"
          "<p style='opacity:.6;font-size:.85rem;margin-top:8px'>" +
          String(t_hint2) + "</p></div>";

  /* ------------------------------------------------------------
     SEZIONE: COUNTDOWN
  ------------------------------------------------------------ */
  page += "<div class='card'><h3>" + String(t_count) + "</h3>";

  for (int i = 0; i < 8; i++) {
    page += "<div class='row'>"

            "<div><label>" + String(t_name) + String(i + 1) + "</label>"
            "<input name='cd" + String(i + 1) + "n' value='" +
            sanitizeText(cd[i].name) + "'/></div>"

            "<div><label>" + String(t_time) + String(i + 1) + "</label>"
            "<input name='cd" + String(i + 1) + "t' type='datetime-local' value='" +
            sanitizeText(cd[i].whenISO) + "'/></div>"

            "</div>";
  }

  page += "<p style='margin-top:14px'>"
          "<button class='btn primary' type='submit'>" + String(t_savebtn) +
          "</button> "
          "<a class='btn ghost' href='/'>" + String(t_home) + "</a>"
          "</p></form></div>";

  /* ------------------------------------------------------------
     SEZIONE BACKUP (export/import JSON)
  ------------------------------------------------------------ */
  page += "<div class='card'><h3>" + String(t_backup) + "</h3>"
          "<p><a class='btn ghost' href='/export'>" + String(t_export) + "</a></p>"

          "<h3>" + String(t_import) + "</h3>"
          "<form method='POST' action='/import' enctype='multipart/form-data'>"
          "<input type='file' name='file' accept='application/json'/>"
          "<p style='margin-top:10px'><button class='btn primary' type='submit'>" +
          String(t_load) + "</button></p></form></div>";

  page += "</main></body></html>";
  return page;
}


/* ============================================================================
   htmlHome()
   Pagina minimale per la home in modalità STA:
     - mostra impostazioni attive
     - riepiloga pagine abilitate
     - punto d’ingresso alla WebUI
============================================================================ */
static String htmlHome() {
  uint32_t s = PAGE_INTERVAL_MS / 1000;
  String enabledList;

  const char* names[PAGES] = {
    "Meteo", "Aria", "Orologio", "Calendario",
    "BTC", "Frase", "Info", "Countdown", "FX", "T24", "Sun", "News"
  };

  for (int i = 0; i < PAGES; i++) {
    if (g_show[i]) {
      if (enabledList.length()) enabledList += ", ";
      enabledList += names[i];
    }
  }

  String page;
  page.reserve(512);

  page += "<!doctype html><html><head>"
          "<meta charset='utf-8'/>"
          "<meta name='viewport' content='width=device-width,initial-scale=1'/>"
          "<title>Gat Multi Ticker</title>"
          "<style>body{font-family:system-ui,Segoe UI,Roboto,Ubuntu}"
          "</style></head><body>"
          "<h2>Gat Multi Ticker</h2>";

  page += "<p><b>Citta:</b> " + sanitizeText(g_city) +
          "<br><b>Lingua:</b> " + sanitizeText(g_lang) +
          "<br><b>Intervallo cambio pagina:</b> " + String(s) + " s</p>";

  page += "<p><b>Pagine attive:</b> " + enabledList + "</p>";

  page += "<p><b>Collegamento:</b> " +
          sanitizeText(g_from_station) + " → " +
          sanitizeText(g_to_station) + "</p>";

  if (g_oa_key.length()) {
    page += "<p><b>Quote of the day:</b> OpenAI (tema: " +
            sanitizeText(g_oa_topic) + ")</p>";
  } else {
    page += "<p><b>Quote of the day:</b> ZenQuotes (default)</p>";
  }

  page += "<p><a href='/settings'>Impostazioni</a></p></body></html>";
  return page;
}


/* ============================================================================
   ROOT HANDLERS
   - STA mode: mostra htmlHome()
   - AP mode: mostra htmlAP()
============================================================================ */
static void handleRootSTA() {
  web.send(200, "text/html; charset=utf-8", htmlHome());
}


/* ============================================================================
   EXPORT CONFIG
   - Genera JSON completo delle impostazioni
   - File scaricabile via browser
============================================================================ */
static void handleExport() {
  String json;
  json.reserve(1024);

  json  = "{";
  json += "\"city\":\"";     json += jsonEscape(g_city);     json += "\",";
  json += "\"lang\":\"";     json += jsonEscape(g_lang);     json += "\",";
  json += "\"ics\":\"";      json += jsonEscape(g_ics);      json += "\",";
  json += "\"lat\":\"";      json += jsonEscape(g_lat);      json += "\",";
  json += "\"lon\":\"";      json += jsonEscape(g_lon);      json += "\",";
  json += "\"page_ms\":";    json += String(PAGE_INTERVAL_MS); json += ",";
  json += "\"oa_key\":\"";   json += jsonEscape(g_oa_key);   json += "\",";
  json += "\"oa_topic\":\""; json += jsonEscape(g_oa_topic); json += "\",";
  json += "\"rss_url\":\"";  json += jsonEscape(g_rss_url);  json += "\",";
  json += "\"pages_mask\":"; json += String(pagesMaskFromArray()); json += ",";
  json += "\"countdowns\":[";

  for (int i = 0; i < 8; i++) {
    if (i > 0) json += ",";
    json += "{\"name\":\"";
    json += jsonEscape(cd[i].name);
    json += "\",\"when\":\"";
    json += jsonEscape(cd[i].whenISO);
    json += "\"}";
  }
  json += "]}";

  web.setContentLength(json.length());
  web.sendHeader("Content-Disposition", "attachment; filename=\"gat_config.json\"");
  web.sendHeader("Content-Type", "application/octet-stream");
  web.sendHeader("Connection", "close");

  web.send(200);
  web.sendContent(json);
  web.client().stop();
}


/* ============================================================================
   IMPORT CONFIG
   - Riceve JSON
   - Aggiorna tutte le impostazioni
   - Aggiorna mask pagine + countdown
============================================================================ */
static void handleImport() {
  if (!web.hasArg("file") || web.arg("file").length() == 0) {
    web.send(400, "text/plain", "Nessun file caricato");
    return;
  }

  String body = web.arg("file");

  // Getter rapidi per campi JSON
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

  // Import campi base
  g_city = getStr("city");
  g_lang = getStr("lang");
  g_ics  = getStr("ics");
  g_lat  = getStr("lat");
  g_lon  = getStr("lon");

  long ms = getNum("page_ms");
  if (ms > 0) PAGE_INTERVAL_MS = ms;

  g_from_station = getStr("from");
  g_to_station   = getStr("to");

  g_oa_key   = getStr("oa_key");
  g_oa_topic = getStr("oa_topic");
  g_rss_url  = getStr("rss_url");

  uint16_t mask = (uint16_t)getNum("pages_mask");
  pagesArrayFromMask(mask);

  // Countdown
  for (int i = 0; i < 8; i++) {
    String tagN = "\"name\":\"";
    String tagT = "\"time\":\"";

    int block = body.indexOf("{", body.indexOf("\"cd\""));
    for (int k = 0; k < i; k++)
      block = body.indexOf("{", block + 1);

    if (block < 0) break;

    int bn = body.indexOf(tagN, block);
    int bt = body.indexOf(tagT, block);
    if (bn < 0 || bt < 0) break;

    int s1 = bn + tagN.length();
    int e1 = body.indexOf("\"", s1);
    int s2 = bt + tagT.length();
    int e2 = body.indexOf("\"", s2);

    cd[i].name    = (e1 > s1 ? body.substring(s1, e1)   : "");
    cd[i].whenISO = (e2 > s2 ? body.substring(s2, e2)   : "");
  }

  g_dataRefreshPending = true;

  web.send(200, "text/html; charset=utf-8",
           "<!doctype html><meta charset='utf-8'><body>"
           "<h3>Impostazioni importate.</h3>"
           "<p><a href='/settings'>Torna alle impostazioni</a></p>"
           "</body>");
}


/* ============================================================================
   ATTIVAZIONE PORTALE (AP MODE)
============================================================================ */
static void startDNSCaptive() {
  dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
}

static void startAPPortal() {
  web.on("/",       HTTP_GET,  handleRootAP);
  web.on("/save",   HTTP_POST, handleSave);
  web.on("/reboot", HTTP_GET,  handleReboot);
  web.on("/export", HTTP_GET,  handleExport);

  web.onNotFound(handleRootAP);
  web.begin();
}


/* ============================================================================
   ATTIVAZIONE WEBUI (STA MODE)
============================================================================ */
static void startSTAWeb() {
  web.on("/",          HTTP_GET,  handleRootSTA);
  web.on("/settings",  HTTP_ANY,  handleSettings);
  web.on("/force_qod", HTTP_POST, handleForceQOD);
  web.on("/import",    HTTP_POST, handleImport);

  web.onNotFound(handleRootSTA);
  web.begin();
}
