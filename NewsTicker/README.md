# Gat News Ticker (ESP32-4848S040)

Ticker di notizie per pannello **ESP32-4848S040**.

<img src="https://github.com/user-attachments/assets/00f90700-a2e5-4562-84fd-fa016f371299"
     width="500">


## Requisiti hardware

* Board: ESP32-4848S040
  
---

## Dipendenze (Arduino IDE / PlatformIO)

* `Arduino_GFX_Library`
* `DNSServer`
* `WebServer` (ESP32)
* `WiFi` (ESP32)
* `HTTPClient`
* `Preferences`

---

## Build: impostazioni consigliate (Arduino IDE)

* Scheda: **ESP32S3 Dev Module**
* USB CDC On Boot: **Enabled**
* Flash Size: **8MB** (o quella del tuo modulo)
* PSRAM: **Enabled** (se presente)
* Upload Speed: **921600** (o 460800 se instabile)
* Partition Scheme: **Default 4MB/8MB** (come da modulo)

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
2. Collegati all’AP dal telefono/PC con il codice QR a schermo. Si aprirà la pagina per inserire **SSID** e **Password** della tua rete.
3. Le credenziali vengono salvate; il dispositivo si riavvia e si collega alla rete.

> Se ricompili cambiando solo i feed, **le credenziali rimangono**: non devi ripetere il login al wifi domestico.

---

## Licenza

Questo progetto è distribuito sotto licenza
**Creative Commons Attribuzione – Non commerciale 4.0 Internazionale (CC BY-NC 4.0)**.

Puoi:

* **Condividere** — copiare e ridistribuire il materiale in qualsiasi formato o mezzo.
* **Adattare** — remixare, trasformare e sviluppare il materiale.

A condizione di:

* **Attribuzione** — devi fornire un’adeguata attribuzione all’autore originale (Davide Nasato / [davidegat](https://github.com/davidegat)), includendo un link alla licenza.
* **Non commerciale** — non puoi utilizzare il materiale per scopi commerciali.

👉 Testo completo della licenza:
[https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)
