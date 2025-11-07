Sono Camilla.

# Gat News Ticker (ESP32-S3 480×480)

Ticker di notizie per pannello **ESP32-S3 + LCD 480×480 (ST7701 RGB)**.
Mostra i titoli presi da 4 feed RSS, impagina **4 notizie per pagina** (testo arancione su sfondo nero), cambia pagina ogni **30 s** e aggiorna l’elenco ogni **10 minuti** con **ordine random**.
Al primo avvio crea un **Access Point** con **captive portal** per inserire SSID e password: le credenziali vengono salvate in **NVS/Preferences** e riutilizzate ai riavvii.

---

## Caratteristiche

* Pannello **480×480 RGB** (controller ST7701 – init “type9”) pilotato con **Arduino_GFX**.
* **Wi-Fi provisioning** via AP + captive portal (DNSServer + WebServer).
* Lettura **RSS** (HTTPClient), parsing semplice `<item><title/><link/></item>`.
* **Dedup** titoli/link, **shuffle globale**, 4 titoli/pagina / 30 s, **refresh ogni 10 min**.
* Normalizzazione caratteri: tutte le virgolette/apici vengono convertite in **apostrofo semplice `'`** per evitare artefatti di rendering sui font base.

---

## Requisiti hardware

* Board: **ESP32-S3** con bus **RGB 16-bit** collegato a pannello **480×480** (pinout come nello sketch).
* Retroilluminazione su pin **38** (PWM).

> Il progetto è pensato per la famiglia di moduli stile **ESP32-S3 4.8" 480×480** (ST7701).

---

## Dipendenze (Arduino IDE / PlatformIO)

* `Arduino_GFX_Library`
* `DNSServer`
* `WebServer` (ESP32)
* `WiFi` (ESP32)
* `HTTPClient`
* `Preferences`

> Se usi la versione con **QR Wi-Fi** a schermo, aggiungi i file `qrcode_wifi.h` e `qrcode_wifi.c` (vedi “Crediti”).

---

## Build: impostazioni consigliate (Arduino IDE)

* Scheda: **ESP32S3 Dev Module**
* USB CDC On Boot: **Enabled**
* Flash Size: **8MB** (o quella del tuo modulo)
* PSRAM: **Enabled** (se presente)
* Upload Speed: **921600** (o 460800 se instabile)
* Partition Scheme: **Default 4MB/8MB** (come da modulo)

---

## Struttura (minima)

```
GatNewsTicker/
├─ NewsTicker.ino
├─ (opzionale) qrcode_wifi.h
└─ (opzionale) qrcode_wifi.c
```

> Nella variante “senza touch/QR” i file `qrcode_wifi.*` non sono obbligatori. Inseriscili solo se vuoi visualizzare un QR per la rete AP sulla schermata di provisioning.

---

## Configurazione dei feed RSS

I feed sono nel sorgente, all’inizio del file (`FEEDS`):

```cpp
const char* FEEDS[4] = {
  "https://www.ansa.it/sito/ansait_rss.xml",
  "https://www.ilsole24ore.com/rss/mondo.xml",
  "https://www.ilsole24ore.com/rss/italia.xml",
  "https://www.fanpage.it/feed/"
};
```

* Puoi **sostituire** uno o più URL; se ne **mancano** alcuni (stringa vuota), l’app **ignora** quel posto senza errori.
* Dopo aver cambiato gli URL e **riprogrammato**, **non serve rifare la configurazione Wi-Fi**: le credenziali restano salvate in NVS e saranno riutilizzate automaticamente.

---

## Prima esecuzione / Provisioning Wi-Fi

1. Se non trova credenziali salvate, il modulo avvia un **Access Point** (SSID tipo `PANEL-XXXX`) e un **captive portal**.
2. Collegati all’AP dal telefono/PC. Si aprirà la pagina per inserire **SSID** e **Password** della tua rete.
3. Le credenziali vengono salvate; il dispositivo si riavvia e si collega alla rete.

> Se ricompili cambiando solo i feed, **le credenziali rimangono**: non devi ripetere il provisioning.

---

## Uso

* A connessione Wi-Fi attiva, l’app scarica i feed, fa dedup + shuffle e mostra **4 titoli per pagina**.
* Cambia pagina ogni **30 secondi**.
* Ogni **10 minuti** riscarica tutti i feed e ricostruisce l’elenco random.

---

## Note tecniche

* Rendering: font base con “bold” simulato (ridisegno sfalsato), colore **arancione** su **nero**.
* Word-wrap manuale in box, con altezza uniforme per 4 righe/blocchi verticali.
* Parser RSS volutamente **tollerante**: gestisce `<item>` standard; se un feed è “anomalo”, tenta un fallback su `<title>/<link>` sequenziali.
* Normalizzazione testo: rimozione tag XML/CDATA, decoding entità (`&amp;`, `&quot;`, ecc.), riduzione accenti a ASCII base, **mappatura di tutte le virgolette/apici a `'`**.

---

## Troubleshooting

* **Schermo nero**: verifica pinout RGB, init ST7701 “type9”, retroilluminazione (pin 38 PWM).
* **Nessun feed**: controlla connettività, URL, certificati/redirect HTTP (i feed devono essere raggiungibili via `HTTPClient`).
* **Captive portal non si apre**: su alcuni telefoni il popup può non partire; apri manualmente l’IP dell’AP indicato a display.

---

## Crediti

* **QR Wi-Fi helper (`qrcode_wifi.h` / `qrcode_wifi.c`)**: derivati dal progetto **“OraQuadra Nano v1.3”** di **Survival Hacking**.
  Utilizzali se vuoi mostrare a schermo un **QR di connessione all’AP** durante il provisioning.
  Inserisci i due file nella stessa cartella dello sketch e include il relativo header nel `.ino`.

---

## Licenza

Scegli una licenza e inseriscila qui (es. MIT). Se includi i file QR dal progetto citato, rispetta i termini della loro licenza e mantieni i crediti.

---

## Modifiche rapide

* Cambia i feed in `FEEDS[]` e ricompila.
* Le **credenziali Wi-Fi non si perdono** con la riprogrammazione (sono in NVS).
* Per “dimenticare” la rete, cancella lo namespace `wifi` in `Preferences` o aggiungi una routine di reset nelle tue build di manutenzione.
