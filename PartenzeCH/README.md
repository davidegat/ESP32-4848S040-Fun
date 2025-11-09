# PartenzeCH

Visualizzatore partenze **realtime** per **ESP32-S3 4848S040**.
Usa l’API pubblica **transport.opendata.ch** per mostrare le prossime connessioni tra due stazioni svizzere, con **captive portal Wi-Fi**, configurazione **via web** del viaggio, sincronizzazione **NTP**.

<img src="https://github.com/user-attachments/assets/a3cd4cac-9d3b-4ccf-b821-af2244d70411"
     width="500">
---

## Funzioni principali

* **Wi-Fi**:
  * **Captive Portal** (SSID `PANEL-XXXX`, pwd `panelsetup`): pagina per inserire SSID e password, salvataggio in NVS e **riavvio automatico**.

* **Configurazione rotta via Web**:
  * Pagina `http://IP/route` con form **Partenza** e **Arrivo**.
  * Salvataggio in NVS, **refresh istantaneo** del pannello.
  * **Fallback** predefinito: *Bellinzona → Lugano*.

* **NTP**: sincronizzazione una volta all’avvio (fuso CH/IT con ora legale).

* **Tabella partenze**:
  * Colonne: **ORA | LINEA | DURATA | CAMBI**.

  
* **UI**:
  * **Header rosso**: “PARTENZE” + **data/ora** allineata a destra.
  * **Barra rotta** sotto l’header con il tragitto.
  * Sub-header colonne con separatore, area dati con righe divisorie.
  
* **Persistenza**: Wi-Fi e rotta in **NVS** (sopravvivono ai reflashing).

---

## Requisiti

* **Hardware**: ESP32-S3 Panel-4848S040 (bus/pin del progetto già impostati).
* **Librerie Arduino**:

  * `Arduino_GFX_Library`
  * `ArduinoJson` (testato con 7.x)
  * Core **esp32** (testato con 2.0.17 o compatibile)

* **Rete**: accesso a Internet (NTP + `transport.opendata.ch`).

---

## Primo avvio (Wi-Fi)

1. Al boot lo sketch tenta la connessione **STA** con le credenziali in NVS.
2. Se fallisce: avvia **AP + Captive Portal**
   * SSID mostrato a schermo (`PANEL-XXXX`), password `panelsetup`.
   * Si apre in automatico il portale; se non compare, visita `http://192.168.4.1/`.
3. Inserisci **SSID** e **Password** → **Salva** → il modulo **si riavvia** e si collega in STA.

---

## Configurare la rotta

* Raggiungi `http://<ip-del-pannello>/route` (in STA o in AP).
* Compila **Partenza** e **Arrivo** (es. “Bellinzona”, “Mendrisio”).
* Conferma con **OK**: i valori sono salvati in NVS e il display si aggiorna subito.
* Se i campi restano vuoti, resta il **fallback Bellinzona → Mendrisio**.

---

## Come funziona il refresh

* Intervallo richieste API: **ogni 2 minuti** (costante `FETCH_EVERY`).
* NTP sincronizzato a inizio esecuzione; l’orario in header si riallinea periodicamente.

> **Nota limiti API**: a 1 richiesta ogni 2 minuti sono ~**720 richieste/giorno** (sotto il tetto tipico di 1000/giorno). Valuta un intervallo più ampio in caso di più pannelli.

---

## Personalizzazione rapida (costanti)

Nel codice:

* **Intervallo aggiornamento**

  ```cpp
  static const uint32_t FETCH_EVERY = 2UL * 60UL * 1000UL; // 2 minuti
  ```

  Esempi: 5 min → `5UL * 60UL * 1000UL`.

* **Numero righe visualizzate**

  ```cpp
  static const int ROWS_MAX = 11; // fino a 11 righe
  ```

  (L’URL API aggiunge un margine per compensare le righe filtrate.)

* **Colori UI**: `RGB565_RED`, `RGB565_WHITE`, ecc.

* **Filtro “entro 60 s”**: funzione `shouldSkipSoon(...)` (incluso fallback ISO HH:MM).

---

## Risoluzione problemi

* **NET ERR: Wi-Fi**
  Perdita di connessione. Verifica SSID/pwd: rifai l’AP cancellando temporaneamente le credenziali in NVS o riprogrammando e impedendo la connessione STA.

* **HTTP/JSON ERR**
  Timeout API o risposta inattesa. Controlla connettività Internet e stabilità DNS. Se hai ridotto la `DynamicJsonDocument` e compare errore di parsing, rialzala (es. 24–32 KB).

* **L’orologio non appare**
  NTP non ancora sincronizzato: lascia qualche secondo, l’header si riallinea in automatico.

* **Portale Wi-Fi non si apre**
  Connettiti all’AP e visita manualmente `http://192.168.4.1/`.

---

## Privacy & sicurezza

* Le credenziali Wi-Fi sono salvate in **NVS**.
* Le richieste HTTPS verso `transport.opendata.ch` usano `WiFiClientSecure` con **`setInsecure()`** (si accetta il certificato senza verifica CA, pratica comune in ambienti embedded per semplicità). In contesti più rigidi caricare e validare la CA.

---

## Crediti

* **Dati**: [transport.opendata.ch](https://transport.opendata.ch/)
* **Grafica**: `Arduino_GFX_Library`
* **JSON**: `ArduinoJson`
* **ESP32 core** di Espressif

---

## Licenza

Questo progetto è distribuito con licenza **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)**.

* **Consentito**: usare, modificare e **ridistribuire** il materiale **per scopi non commerciali**, con **attribuzione**.
* **Non consentito**: uso **commerciale** senza permesso esplicito.
* **Attribuzione richiesta**

Testo completo della licenza: [https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)

---

## Disclaimer

Il software è fornito “così com’è”, senza garanzie di correttezza o disponibilità dei servizi di terze parti. Verifica i termini d’uso di `transport.opendata.ch` prima della distribuzione.
