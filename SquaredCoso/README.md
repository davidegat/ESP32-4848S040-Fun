# SquaredCoso

SquaredCoso è un firmware per pannello TFT 480×480 basato su ESP32-S3, pensato per trasformare il modulo 4848S040 in una dashboard informativa sempre aggiornata. Il progetto cicla automaticamente tra più pagine tematiche, visualizzando dati provenienti da servizi web pubblici e da un calendario ICS remoto.

## Funzionalità principali
- **Paginas multi-sorgente**: meteo corrente e previsioni wttr.in, qualità dell'aria da Open-Meteo, orologi analogici/digitali, eventi del giorno da ICS, quotazioni BTC/CHF, frasi motivazionali ZenQuotes, informazioni di sistema e countdown multipli.
- **Informazioni sui trasporti**: integrazione con `transport.opendata.ch` per mostrare la prossima connessione fra due stazioni configurabili.
- **Sincronizzazione oraria**: aggiornamento NTP automatico con gestione dell'ora legale.
- **Interfaccia ottimizzata**: palette Aurora Borealis ad alto contrasto, testo sanificato e impaginazione con header temporizzato.
- **Configurazione flessibile**: credenziali e preferenze salvate in NVS; captive portal/AP quando le credenziali Wi-Fi non sono disponibili; web UI `/settings` per città, lingua, ICS, countdown, e dettagli della rotta.
- **Gestione countdown**: fino a otto eventi con nome personalizzato e data ISO locale.

## Hardware e dipendenze
- ESP32-S3 con pannello 4848S040 (driver ST7701, interfaccia RGB e retroilluminazione PWM).
- Libreria [`Arduino_GFX`](https://github.com/moononournation/Arduino_GFX) per la gestione del display RGB.
- Dipendenze standard Arduino/ESP32: `WiFi`, `WebServer`, `DNSServer`, `HTTPClient`, `Preferences`, `Wire`, `time`.

## Configurazione e uso
1. Compila e carica `SquaredCoso.ino` con l'IDE Arduino o `arduino-cli`, assicurandoti di avere installato il core ESP32 più recente.
2. Al primo avvio, se non sono presenti credenziali Wi-Fi, il dispositivo crea un access point con captive portal per l'inserimento dei dati.
3. Accedi alla web UI su `http://<indirizzo-dispositivo>/settings` per configurare lingua, località, sorgente ICS, countdown, preferenze di trasporto e intervallo di rotazione delle pagine.
4. Il firmware salverà le impostazioni in NVS e passerà automaticamente alla modalità stazione.

## Fonti dati esterne
- [wttr.in](https://wttr.in) (meteo)
- [Open-Meteo AQI API](https://open-meteo.com)
- [ZenQuotes API](https://zenquotes.io)
- [CoinGecko API](https://www.coingecko.com)
- [transport.opendata.ch](https://transport.opendata.ch)

Assicurarsi di rispettare i termini d'uso dei servizi esterni.

## Struttura del codice
- **Display e PWM**: inizializzazione del pannello ST7701 e controllo retroilluminazione.
- **Sanificazione testo**: normalizzazione di caratteri Unicode/HTML per garantire la visualizzazione corretta.
- **UI**: funzioni per header, testi bold/centrati, layout pagine e paragrafi con word-wrap.
- **Sincronizzazione temporale**: utility per sincronizzare l'orologio via NTP e formattare le date.
- **Networking**: gestione credenziali, captive portal, server web e richieste HTTP verso le API.
- **Persistenza**: uso di `Preferences` per salvare configurazioni, countdown e impostazioni del pannello.

## Licenza
Questo progetto è distribuito sotto la licenza Creative Commons Attribuzione-Non Commerciale 4.0 Internazionale (CC BY-NC 4.0). Consulta il file `LICENSE.md` nella stessa cartella per i dettagli.
