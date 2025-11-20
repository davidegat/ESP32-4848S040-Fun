# SquaredCoso

SquaredCoso è un firmware per pannelli TFT 480×480 basati su ESP32-S3 che trasforma il modulo 4848S040 in una dashboard informativa sempre aggiornata. Il dispositivo alterna automaticamente pagine tematiche con meteo, orologio, trasporti, frasi motivazionali, stato di sistema e countdown personalizzati.

## Panoramica del funzionamento
Il firmware inizializza il display ST7701 in modalità RGB, avvia la connessione Wi-Fi e sincronizza l'orologio tramite NTP. Una volta ottenuta la rete, scarica ciclicamente i dati dalle API configurate, li normalizza e li presenta su pagine grafiche ottimizzate per la leggibilità a distanza. Ogni pagina rimane visibile per l'intervallo definito nelle impostazioni e viene aggiornata quando le sorgenti forniscono nuovi dati.

## Funzionalità principali
- **Rotazione automatica delle pagine** con temi meteo, calendario ICS, countdown, traffico ferroviario e informazioni di sistema.
- **Selezione pagine personalizzata** direttamente dall'interfaccia web per includere o escludere i widget dalla rotazione.
- **Widget meteo e qualità dell'aria** con icone condensate, temperatura, percepita, precipitazioni e valori AQI suddivisi per inquinante.
- **Pagina trasporti** con la prossima partenza dalla stazione di origine alla destinazione configurata, inclusi binario, ritardo e durata del viaggio.
- **Gestione countdown** per fino a otto eventi futuri con titolo, data/ora e facoltativo promemoria anticipato.
- **Widget quotazioni e citazioni** con conversione BTC/CHF via CoinGecko e frasi motivazionali giornaliere da ZenQuotes.
- **Monitor di sistema** che mostra uptime, intensità retroilluminazione, Wi-Fi RSSI, consumo memoria e stato delle API.
- **Grafico evoluzione temperatura** sui prossimi giorni basato sulle medie Open-Meteo, con interpolazione 24 slot.
- **Ore di luce giornaliere** con orari di alba, tramonto e durata totale del giorno.
- **Cambio rapido CHF** verso EUR/USD/GBP/JPY tramite API Frankfurter.
- **Sincronizzazione oraria affidabile** con gestione automatica dell'ora legale, fallback a RTC interno e icona di stato.
- **Interfaccia `/settings` integrata** per modificare al volo lingua, città di riferimento, URL ICS, elenco countdown, città trasporto e velocità rotazione.

## Preparazione dell'hardware
- **Pannello**: modulo 4848S040 con driver ST7701, bus RGB a 16 bit e retroilluminazione PWM.
- **Microcontrollore**: ESP32-S3 con sufficiente PSRAM (8 MB consigliati) per framebuffer e buffer HTTP.
- **Alimentazione**: 5 V stabile con almeno 2 A di corrente per gestire picchi del display.
- **Connessioni**: seguire la mappatura dei pin indicata in `SquaredCoso.ino` (`RGB_PIN_*`, `DE_PIN`, `PCLK_PIN`, `BACKLIGHT_PIN`).

## Dipendenze software
- Core ESP32 per Arduino 3.x o superiore.
- Libreria [`Arduino_GFX`](https://github.com/moononournation/Arduino_GFX) per il rendering del display.
- Librerie standard Arduino: `WiFi`, `WebServer`, `DNSServer`, `HTTPClient`, `Preferences`, `Wire`, `time`, `SPIFFS` (per il portale).
- Facoltative: [`ESPAsyncWebServer`](https://github.com/me-no-dev/ESPAsyncWebServer) se si desidera migrare a versione asincrona.

## Compilazione e caricamento
1. Installare il core ESP32-S3 nell'IDE Arduino o in `arduino-cli`.
2. Copiare la cartella `SquaredCoso` nella directory degli sketch Arduino o aprirla direttamente nell'IDE.
3. Selezionare la scheda "ESP32S3 Dev Module" (o equivalente con PSRAM attiva) e impostare `PSRAM` su "Enabled".
4. Compilare il progetto. In caso di errori legati a memoria, verificare che la PSRAM sia abilitata e che la velocità PSRAM sia impostata su 80 MHz.
5. Caricare lo sketch tramite USB-C; il monitor seriale a 115200 bps mostrerà i log di boot e le richieste HTTP.

## Prima configurazione
1. All'accensione, se non trova credenziali Wi-Fi salvate in NVS, il firmware crea un access point `SquaredCoso-XXXX` con captive portal.
2. Collegarsi con smartphone o PC, aprire una pagina web qualsiasi e seguire il reindirizzamento per inserire SSID e password.
3. Dopo il riavvio, raggiungere `http://<indirizzo-ip>/settings` sulla rete locale.
4. Impostare:
   - **Lingua** (IT, EN, DE) per UI e testi statici.
   - **Località meteo** (supporta query wttr.in come `Milano` o `@45.46,9.19`).
   - **URL calendario ICS** per eventi quotidiani.
   - **Countdown** con nome, data ISO (`AAAA-MM-GGTHH:MM`), colore opzionale e icona.
   - **Trasporti** con stazioni origine/destinazione, margine minuti e preferenza per trasporto pubblico.
   - **Durata pagina** in secondi e luminosità predefinita.
5. Salvare: il dispositivo riavvia il ciclo pagine applicando le nuove impostazioni.

## Aggiornamento dei contenuti
- **Meteo**: interrogazione a `wttr.in` ogni 15 minuti; in caso di errore mostra l'ultima previsione valida.
- **Qualità dell'aria**: richiesta a Open-Meteo AQI ogni 30 minuti con fallback al valore precedente.
- **Calendario ICS**: scaricato all'avvio e poi ogni ora; gli eventi passati vengono filtrati automaticamente.
- **Countdown**: aggiornati localmente ogni secondo; allo scadere viene mostrato un messaggio evidenziato.
- **Trasporti**: interrogazione a `transport.opendata.ch` 2 minuti prima della scadenza della corsa corrente.
- **Quote**: frasi ZenQuotes e BTC/CHF aggiornati ogni ora per ridurre il rate limit.
- **Grafico temperatura**: medie giornaliere da Open-Meteo ricalcolate a ogni ciclo di refresh.
- **Ore di luce**: alba/tramonto da Sunrise-Sunset aggiornati insieme al resto dei dati periodici.
- **Cambio CHF**: tassi EUR/USD/GBP/JPY da Frankfurter su ogni refresh completo.

## Personalizzazione avanzata
- Modificare colori, font e layout intervenendo sulle costanti `PALETTE_*` e sulle funzioni di rendering nel file `.ino`.
- Attivare o disattivare pagine specifiche dall'interfaccia `/settings`, oppure intervenire manualmente sull'array `pageSequence`.
- Integrare nuove API duplicando il pattern `fetchXXX()` e aggiungendo il relativo widget nel ciclo pagine.
- Utilizzare la seriale per debug: log strutturati preceduti da `[PAGE]`, `[NET]`, `[UI]` facilitano la diagnostica.

## Risoluzione problemi
- **Schermo bianco/nero**: verificare cablaggio RGB e la corretta inizializzazione del pannello in `setupDisplay()`.
- **Loop di riavvio**: controllare alimentazione e messaggi `Guru Meditation` sulla seriale; spesso legati a stack insufficiente.
- **Errore API**: la pagina mostra un'icona gialla con codice HTTP; verificare con `curl` la raggiungibilità del servizio.
- **Wi-Fi instabile**: ridurre la luminosità (influenza i consumi), scegliere canali meno affollati o spostare l'antenna.
- **Aggiornamenti lenti**: aumentare l'intervallo pagina nelle impostazioni per concedere più tempo al caricamento delle API.

## Fonti dati esterne
- [wttr.in](https://wttr.in) per meteo e previsioni a breve termine.
- [Open-Meteo AQI API](https://open-meteo.com) per indici di qualità dell'aria.
- [Open-Meteo](https://open-meteo.com) per le temperature medie dei prossimi giorni.
- [ZenQuotes API](https://zenquotes.io) per citazioni quotidiane.
- [CoinGecko API](https://www.coingecko.com) per quotazioni BTC/CHF.
- [transport.opendata.ch](https://transport.opendata.ch) per pianificazione dei trasporti pubblici svizzeri.
- [Sunrise-Sunset.org](https://sunrise-sunset.org/api) per alba, tramonto e durata del giorno.
- [Frankfurter.app](https://www.frankfurter.app) per i cambi CHF verso principali valute.
- Eventuale calendario ICS privato o pubblico (Google Calendar, iCloud, Nextcloud, ecc.).

Assicurarsi di rispettare i termini d'uso dei servizi e di impostare cache locale se l'uso è intensivo.

## Struttura del codice
- **`setup()`** gestisce inizializzazione hardware, montaggio filesystem, caricamento preferenze e avvio pagine.
- **`loop()`** richiama la macchina a stati che ruota le pagine e gestisce i timer di refresh.
- **Modulo display**: funzioni per palette Aurora Borealis, rasterizzazione testo, buffer doppio e fading della retroilluminazione.
- **Modulo rete**: funzioni `connectWiFi()`, `startCaptivePortal()`, `fetchJson()` per chiamate HTTP robuste con retry.
- **Modulo tempo**: sincronizzazione NTP, parsing ICS, calcolo countdown e formattazione localizzata (lingua/locale).
- **Persistenza**: wrapper `Preferences` per salvare JSON compressi con le impostazioni utente.

## Aggiornamenti firmware
- **OTA manuale**: ricompilare lo sketch e caricarlo via USB quando si modificano funzioni o si aggiornano le librerie.
- **Backup impostazioni**: dalla pagina `/settings/export` è possibile scaricare il JSON delle configurazioni.
- **Ripristino**: tenere premuto il pulsante BOOT per 5 secondi all'avvio per cancellare le preferenze e riaprire il captive portal.

## Licenza
Il progetto è distribuito con licenza Creative Commons Attribuzione-Non Commerciale 4.0 Internazionale (CC BY-NC 4.0). I dettagli completi sono disponibili nel file `LICENSE.md`.

---

# SquaredCoso (English)

SquaredCoso is firmware for 480×480 RGB TFT panels driven by an ESP32-S3. It turns the 4848S040 module into a self-updating information kiosk that rotates through themed pages featuring weather, clocks, transport data, motivational quotes, system health, and custom countdowns.

## How it works
The firmware boots the ST7701 display in RGB mode, connects to Wi-Fi, and synchronizes time via NTP. Once online it polls the configured APIs on a schedule, sanitizes the received data, and renders polished pages optimized for distance readability. Each page stays on screen for the configured interval and refreshes whenever new data is available.

## Core features
- **Automatic page rotation** covering weather, ICS calendar, countdowns, Swiss transport departures, and system diagnostics.
- **Custom page selection** straight from the web interface to include or exclude widgets from the rotation.
- **Weather & air-quality widgets** with compact icons, temperature, feels-like data, precipitation, and pollutant-specific AQI.
- **Transport page** with the next departure from the origin station to the destination, including platform, delay, and travel time.
- **Countdown manager** supporting up to eight events with title, date/time, and optional early reminder.
- **Quotes & rates** combining BTC/CHF conversion via CoinGecko and a daily motivational message from ZenQuotes.
- **System monitor** reporting uptime, backlight intensity, Wi-Fi RSSI, memory usage, and API health indicators.
- **Temperature trend chart** for the coming days using Open-Meteo daily means, interpolated across 24 slots.
- **Daylight hours** page showing sunrise, sunset, and total light duration.
- **Fast CHF exchange** page toward EUR/USD/GBP/JPY via the Frankfurter API.
- **Robust timekeeping** with DST-aware NTP sync, on-device RTC fallback, and a visible status icon.
- **Integrated `/settings` interface** to adjust language, weather location, ICS URL, countdown list, transport route, and page timing.

## Hardware preparation
- **Display**: 4848S040 module (ST7701 driver, 16-bit RGB bus, PWM backlight).
- **Controller**: ESP32-S3 with sufficient PSRAM (8 MB recommended) for framebuffers and HTTP buffers.
- **Power**: regulated 5 V supply providing at least 2 A to cover display peaks.
- **Wiring**: follow the pin assignments defined in `SquaredCoso.ino` (`RGB_PIN_*`, `DE_PIN`, `PCLK_PIN`, `BACKLIGHT_PIN`).

## Software dependencies
- ESP32 Arduino core version 3.x or newer.
- [`Arduino_GFX`](https://github.com/moononournation/Arduino_GFX) for display handling.
- Standard Arduino libraries: `WiFi`, `WebServer`, `DNSServer`, `HTTPClient`, `Preferences`, `Wire`, `time`, `SPIFFS` (for the captive portal).
- Optional: [`ESPAsyncWebServer`](https://github.com/me-no-dev/ESPAsyncWebServer) if you plan to migrate to an async HTTP stack.

## Build and upload
1. Install the ESP32-S3 board package in the Arduino IDE or `arduino-cli`.
2. Copy the `SquaredCoso` folder to your Arduino sketchbook or open it directly.
3. Select "ESP32S3 Dev Module" (or another PSRAM-capable board) and set `PSRAM` to "Enabled".
4. Compile the sketch. If you hit memory errors, double-check PSRAM settings and ensure the PSRAM speed is set to 80 MHz.
5. Upload over USB-C; open the serial monitor at 115200 bps to watch boot logs and HTTP activity.

## First-time setup
1. On boot, if no Wi-Fi credentials are found in NVS, the firmware creates an access point named `SquaredCoso-XXXX` with a captive portal.
2. Connect with a phone or laptop, open any webpage, and follow the redirect to enter SSID and password.
3. After the reboot, visit `http://<device-ip>/settings` on your LAN.
4. Configure:
   - **Language** (IT, EN, DE) for UI strings.
   - **Weather location** (wttr.in queries such as `Milan` or `@45.46,9.19`).
   - **ICS calendar URL** for daily events.
   - **Countdowns** with title, ISO date (`YYYY-MM-DDTHH:MM`), optional color, and icon.
   - **Transport** origin/destination stations, grace period in minutes, and public transport preference.
   - **Page duration** in seconds and default brightness.
5. Save to restart the page cycle with the new settings applied.

## Data refresh cadence
- **Weather**: fetched from `wttr.in` every 15 minutes; falls back to the last valid forecast on errors.
- **Air quality**: polled from Open-Meteo AQI every 30 minutes with graceful fallback.
- **ICS calendar**: downloaded at boot and then hourly; past events are filtered automatically.
- **Countdowns**: updated locally every second; when an event expires the UI shows a highlighted message.
- **Transport**: queries `transport.opendata.ch` two minutes before the current journey expires.
- **Quotes & rates**: refreshed every hour to respect API rate limits.
- **Temperature chart**: Open-Meteo daily means recalculated on every refresh cycle.
- **Daylight**: sunrise/sunset from Sunrise-Sunset fetched alongside the periodic data refresh.
- **CHF FX**: EUR/USD/GBP/JPY rates from Frankfurter on each full refresh.

## Advanced customization
- Tweak colors, fonts, and layout via the `PALETTE_*` constants and drawing functions in the `.ino` file.
- Enable or disable specific pages from the `/settings` interface, or fall back to editing the `pageSequence` array manually.
- Add new APIs by cloning the `fetchXXX()` pattern and registering a widget in the rotation loop.
- Use the serial monitor for diagnostics: logs prefixed with `[PAGE]`, `[NET]`, and `[UI]` help track issues quickly.

## Troubleshooting
- **Blank or flickering display**: inspect RGB wiring and `setupDisplay()` configuration.
- **Boot loops**: check power delivery and look for `Guru Meditation` errors on serial (usually stack or PSRAM issues).
- **API errors**: yellow warning icon shows the HTTP status code; confirm service availability with `curl`.
- **Unstable Wi-Fi**: reduce backlight to lower current draw, switch to a less crowded channel, or reposition the antenna.
- **Slow updates**: increase the page duration setting to give each API more time to respond.

## External data sources
- [wttr.in](https://wttr.in) for weather and short-term forecasts.
- [Open-Meteo AQI API](https://open-meteo.com) for air-quality indices.
- [Open-Meteo](https://open-meteo.com) for upcoming-day temperature means.
- [ZenQuotes API](https://zenquotes.io) for daily motivational quotes.
- [CoinGecko API](https://www.coingecko.com) for BTC/CHF exchange rates.
- [transport.opendata.ch](https://transport.opendata.ch) for Swiss public transport planning.
- [Sunrise-Sunset.org](https://sunrise-sunset.org/api) for sunrise, sunset, and daylight duration.
- [Frankfurter.app](https://www.frankfurter.app) for CHF exchange against major currencies.
- Private or public ICS calendars (Google Calendar, iCloud, Nextcloud, etc.).

Respect the external services' terms of use and consider caching if you plan heavy usage.

## Code structure overview
- **`setup()`** handles hardware initialization, filesystem mounting, preference loading, and page engine startup.
- **`loop()`** drives the state machine that rotates pages and manages refresh timers.
- **Display module**: palette definitions, text rendering, double buffering, and backlight fading helpers.
- **Networking module**: `connectWiFi()`, `startCaptivePortal()`, and `fetchJson()` provide resilient HTTP access with retry logic.
- **Time module**: NTP synchronization, ICS parsing, countdown math, and localized formatting.
- **Persistence**: `Preferences` wrapper storing compressed JSON with user settings.

## Firmware updates
- **Manual OTA**: rebuild the sketch and flash over USB whenever you update features or dependencies.
- **Backup settings**: use `/settings/export` to download the configuration JSON.
- **Factory reset**: hold the BOOT button for 5 seconds during startup to erase preferences and reopen the captive portal.

## License
This project is released under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0) license. Full details are available in `LICENSE.md`.
