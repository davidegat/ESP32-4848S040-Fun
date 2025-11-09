# PartenzeCH

Visualizzatore di orario dei mezzi di trasporto svizzeri, in **realtime** per **ESP32-S3 4848S040** con pannello ST7701 (init *type9*). Mostra le prossime connessioni tra due stazioni svizzere usando l’API pubblica **transport.opendata.ch**, con **captive portal Wi-Fi**, configurazione **via web**, sincronizzazione **NTP**, **preset di rotta** in NVS e **ciclo preset via touch** (GT911).

---

## Funzioni

### Rete & Portale

* **STA + NVS**: tenta la connessione con credenziali salvate in `NVS` (`wifi/ssid`, `wifi/pass`).
* **Fallback AP + Captive Portal** se non trova credenziali:
  * SSID: `PANEL-XXXX` (ultimi 2 byte MAC), **pwd**: `panelsetup`.
  * Pagina di configurazione Wi-Fi con salvataggio in NVS e **riavvio automatico**.
  * DNS *captive* per intercettare la prima navigazione.

### Configurazione rotta via Web

* Pagina `http://<IP>/route`:
  * Campi **Partenza** e **Arrivo** con salvataggio in `NVS` (`route/from`, `route/to`).
  * **Refresh (s)** personalizzabile (salvato in `route/refresh`).
  * **Aggiornamento istantaneo** del pannello dopo il salvataggio.
  * **Gestione preset** (vedi sotto).
* **Fallback** iniziale: *Bellinzona → Lugano*.

### Preset rotta (NVS) + Touch cycle

* Preset memorizzati in `NVS` namespace `presets` dentro una JSON list (`presets/list`).
* **Web UI**: aggiungi/modifica/elimina preset; applicazione immediata al display.
* **Touch GT911**:
  * **Tap singolo**: cicla i preset in RAM (**senza scrivere in NVS**) avanti e dietro toccando i bordi destro e sinistro.
  
### Tabella partenze
* Colonne: **ORA | LINEA | DURATA | CAMBI**.
* **Filtro “entro 60 s”**: scarta la partenza nell’istante attuale o entro 60 s.
* **Ritardi**: se presente ritardo stimato, la **riga viene disegnata con testo rosso** (altrimenti bianco).
* Background area dati in **blu** stile display dei trasporti pubblici svizzeri.

### Persistenza
* **Wi-Fi**: `wifi/ssid`, `wifi/pass`.
* **Rotta**: `route/from`, `route/to`, `route/refresh` (secondi).
* **Preset**: `presets/list` (JSON con array di `{label, from, to}`).

---

## Requisiti

### Hardware

* **ESP32-S3 Panel-4848S040** (pin/bus già impostati nello sketch).
* Touch **GT911** (INT/RST opzionali: se non presenti, usare `-1`).

### Software / Librerie Arduino
* **ESP32 core** Espressif (testato con **2.0.17** o compatibile).
* **Arduino_GFX_Library** (pannello ST7701 *type9* + `Arduino_ESP32RGBPanel`).
* **ArduinoJson** **7.x** (testato 7.x).
* **TAMC_GT911** (driver touch).
* **WiFi**, **WebServer**, **DNSServer**, **HTTPClient**, **WiFiClientSecure**.
* `time.h` per NTP.

> **Nota TLS**: le richieste HTTPS usano `WiFiClientSecure.setInsecure()`.

---

## Installazione & Primo avvio

1. Compila e flasha con **Board**: ESP32S3 (Dev Module) e PSRAM attiva se disponibile.
2. Al boot:

   * Se in **STA** con credenziali valide → mostra subito interfaccia e scarica dati.
   * Se **no credenziali** → **AP + Captive Portal**:

     * Connettiti a **`PANEL-XXXX`**, **pwd** `panelsetup`.
     * Compila SSID/password e salva (riavvio automatico).
     * In caso di mancato popup, apri `http://192.168.4.1/`.

---

## Configurazione della rotta

* Vai a `http://<IP>/route` (sia in AP sia in STA).
* Compila **Partenza** e **Arrivo** (es. “Bellinzona”, “Mendrisio”).
* Imposta **Refresh (s)** tra **60** e **3600** (default **300**).
* Salva → aggiornamento **immediato** del display.

---

## Preset: web + touch

* **Web**: sezione “Preset” per creare/modificare/eliminare (max 12).
* **Touch**: **tap singolo** cicla i preset **in memoria** (non scrive in NVS).

---

## Aggiornamenti automatici

* Scaricamento dati: ogni **`g_refreshSec`** (default **300 s**).
* Aggiornamento header/data-time: ~**30 s**.
* Il filtro “entro 60 s” evita di mostrare partenze che stanno “scadendo”.

> **Budget richieste**: 300 s ≈ **288 richieste/giorno** per pannello (ampiamente sotto tetti tipici, il limite è 1000). Riduci la frequenza se usi più unità o se aggiorni di frequente cambiando i preset.

---

## Sicurezza & Privacy

* Credenziali Wi-Fi memorizzate in **NVS** (non volatile).
* HTTPS con **verifica disabilitata** (`setInsecure()`): semplice e diffuso in embedded, ma in contesti sensibili carica la **CA** e abilita la verifica certificato.

---

## Risoluzione problemi

* **NET ERR: Wi-Fi**
  Verifica SSID/password. Se serve, rientra in AP cancellando le credenziali o riflashando.

* **HTTP/JSON ERR**
  Connettività Internet assente o risposta inattesa. Aumenta il timeout, verifica DNS e **Dimensione `DynamicJsonDocument`** (usa 32 KB come nello sketch).

* **Nessuna ora in header**
  Attendi la sincronizzazione NTP. L’header viene ridisegnato periodicamente.

* **Portal non si apre**
  Apri manualmente `http://192.168.4.1/` quando connesso all’AP.

---

## Crediti

* **Dati**: transport.opendata.ch

---

## Licenza

Distribuito con **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**.
Consentito uso, modifica e ridistribuzione **non commerciale** con **attribuzione**.
Testo completo: [https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)

---

## Disclaimer

Software fornito “così com’è”. Verifica i termini d’uso di `transport.opendata.ch` prima della distribuzione.
