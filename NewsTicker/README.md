# Gat News Ticker – ESP32-S3 Panel-4848S040

Ticker di notizie RSS per pannello ESP32-S3 4848S040 (480×480 ST7701) con configurazione via Web UI.

<img src="https://github.com/user-attachments/assets/00f90700-a2e5-4562-84fd-fa016f371299" width="500">

---

## Descrizione

Il progetto mostra in tempo reale notizie da feed RSS aggiornate automaticamente sul display.  
Include una pagina Web per configurare fino a otto feed RSS con limiti individuali, salvati in memoria NVS.

---

## Funzioni principali

- Display 480×480 pilotato con **Arduino_GFX_Library** (ST7701 type9).
- Interfaccia grafica con barra superiore blu, testo giallo, notizie bianche e separatori verdi.
- Quattro notizie per pagina con cambio automatico ogni 30 secondi.
- Aggiornamento automatico ogni 10 minuti e refresh immediato dopo il salvataggio da Web UI.
- Captive portal per configurazione Wi-Fi (solo in modalità AP).
- Pagina unica `/rss` per modificare fino a otto feed con limite per feed.
- Configurazione salvata in **NVS** (`url0..url7`, `limit0..limit7`, `n`).

---

## Modalità operative

### Access Point (AP)
- Avviato solo se mancano credenziali Wi-Fi.
- SSID tipo `PANEL-XXXX`, password `panelsetup`.
- Pagina captive per inserire SSID e password.

### Stazione (STA)
- Connessione automatica alla rete salvata.
- Web UI accessibile su `http://<IP>/rss`.
- Salvataggio inline con conferma sulla stessa pagina e aggiornamento immediato del display.

---

## Dipendenze

- Arduino_GFX_Library  
- DNSServer  
- WebServer (ESP32)  
- WiFi (ESP32)  
- HTTPClient  
- Preferences

---

## Build

- Scheda: **ESP32S3 Dev Module**
- USB CDC On Boot: Enabled
- Flash Size: 8MB (o quella del modulo)
- PSRAM: Enabled (se presente)
- Upload Speed: 921600 (o 460800 se instabile)
- Partition Scheme: Default 4MB/8MB

---

## Flusso operativo

1. All’avvio tenta la connessione Wi-Fi; se fallisce, avvia AP e captive portal.
2. In modalità STA sincronizza l’orario (NTP) e scarica i feed RSS.
3. Mostra le notizie e cambia pagina ogni 30 secondi.
4. Aggiorna i feed ogni 10 minuti o subito dopo un salvataggio da Web UI.

---

## Licenza

Creative Commons – Attribuzione – Non Commerciale 4.0 Internazionale (CC BY-NC 4.0)  
Autore: **Davide Nasato** ([davidegat](https://github.com/davidegat))  
Licenza completa: https://creativecommons.org/licenses/by-nc/4.0/

---

# Gat News Ticker – ESP32-S3 Panel-4848S040

RSS news ticker for the ESP32-S3 4848S040 panel (480×480 ST7701) with Web UI configuration.

---

## Description

Displays real-time RSS news feeds on the screen, with an integrated Web UI to edit up to eight feeds with per-feed limits.  
Configuration is stored in non-volatile memory (NVS).

---

## Main Features

- 480×480 display driven by **Arduino_GFX_Library** (ST7701 type9).
- UI with blue header bar, yellow text, white news, green separators.
- Four news per page with automatic switch every 30 seconds.
- Auto-refresh every 10 minutes and instant refresh after saving from Web UI.
- Captive portal for Wi-Fi setup (AP mode only).
- Single `/rss` page to configure up to eight feeds with individual limits.
- Settings stored in **NVS** (`url0..url7`, `limit0..limit7`, `n`).

---

## Operating Modes

### Access Point (AP)
- Started only when no Wi-Fi credentials are stored.
- SSID like `PANEL-XXXX`, password `panelsetup`.
- Captive portal page for entering SSID and password.

### Station (STA)
- Automatically connects to saved network.
- Web UI available at `http://<IP>/rss`.
- Inline save confirmation and instant display refresh.

---

## Dependencies

- Arduino_GFX_Library  
- DNSServer  
- WebServer (ESP32)  
- WiFi (ESP32)  
- HTTPClient  
- Preferences

---

## Build

- Board: **ESP32S3 Dev Module**
- USB CDC On Boot: Enabled
- Flash Size: 8MB (or module capacity)
- PSRAM: Enabled (if available)
- Upload Speed: 921600 (or 460800 if unstable)
- Partition Scheme: Default 4MB/8MB

---

## Workflow

1. On boot, tries to connect to Wi-Fi; if it fails, starts AP with captive portal.
2. In STA mode, syncs time via NTP and loads RSS feeds.
3. Displays news, switching page every 30 seconds.
4. Refreshes feeds every 10 minutes or immediately after saving from Web UI.

---

## License

Creative Commons – Attribution – Non Commercial 4.0 International (CC BY-NC 4.0)  
Author: **Davide Nasato** ([davidegat](https://github.com/davidegat))  
Full license: https://creativecommons.org/licenses/by-nc/4.0/
