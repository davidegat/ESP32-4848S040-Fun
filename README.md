# ESP32-S3 Panel-4848S040 · Experiments & Fun
**Autore:** Davide Nasato (gat)

<table>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/fa71b786-9da7-488c-a8c5-b9a4eecb2e9e" width="220" alt="Vista frontale del pannello ESP32-S3"></td>
    <td><img src="https://github.com/user-attachments/assets/00f90700-a2e5-4562-84fd-fa016f371299" width="220" alt="Interfaccia touch in funzione"></td>
  </tr>
  <tr>
    <td><img src="https://github.com/user-attachments/assets/233a7806-4e2d-4e74-b4c7-6af3fe28045c" width="220" alt="Esempio di grafica RGB sul display"></td>
    <td><img src="https://github.com/user-attachments/assets/be0071e5-bf8f-4652-ad98-f1517c20733a" width="220" alt="Applicazione con codici QR"></td>
  </tr>
</table>

---

## Indice

1. [Panoramica](#panoramica)
2. [Progetti inclusi](#progetti-inclusi)
3. [Hardware supportato](#hardware-supportato)
4. [Configurazione dell'IDE Arduino](#configurazione-dellide-arduino)
5. [Licenza](#licenza)

---

## Panoramica

Questo repository raccoglie **esperimenti e strumenti software** per il **pannello ESP32-S3 Panel-4848S040**, che integra:

* microcontrollore **ESP32-S3**;
* **display IPS 480×480** con controller **ST7701**;
* **touch capacitivo GT911**;
* **slot microSD** collegato via **FSPI**.

Gli sketch esplorano grafica RGB, interfacce touch, salvataggio su microSD e controllo della retroilluminazione, fornendo esempi pronti da modificare.

---

## Progetti inclusi

* **`ESP32-S3-HelloWorld/`** – esempio "Hello World" per verificare rapidamente il display e la toolchain.
* **`Fotine/`** – cornice fotografica Wi-Fi che scarica periodicamente immagini e le mostra sul pannello.
* **`NewsTicker/`** – ticker di notizie basato su feed RSS con configurazione Wi-Fi tramite captive portal.
* **`PartenzeCH/`** – tabellone partenze per i trasporti svizzeri con aggiornamento da API pubbliche e interfaccia touch.
* **`Pixxellata/`** – editor di pixel art con palette touch e salvataggio su microSD in JPEG/BMP.
* **`QuadrantiOraQuadra/`** – raccolta di quadranti grafici compatibili con il progetto OraQuadra Nano v1.3.
* **`RandomYoutube/`** – generatore di link e codici QR casuali di YouTube per esperimenti grafici sul pannello.

---

## Hardware supportato

**Scheda:** ESP32-S3 Panel-4848S040 — [Documentazione HomeDing](https://homeding.github.io/boards/esp32s3/panel-4848S040.htm)

### Pin principali (configurazione standard)

| Funzione       | Pin                                        |
| -------------- | ------------------------------------------ |
| I²C Touch      | SDA = 19, SCL = 45                         |
| Retroilluminazione | 38 (PWM LEDC)                          |
| SWSPI (ST7701) | CS = 39, SCK = 48, MOSI = 47               |
| Pannello RGB   | DE = 18, VSYNC = 17, HSYNC = 16, PCLK = 21 |
| Canali R       | 11, 12, 13, 14, 0                          |
| Canali G       | 8, 20, 3, 46, 9, 10                        |
| Canali B       | 4, 5, 6, 7, 15                             |
| SD (FSPI)      | CS = 42, MOSI = 47, MISO = 41, SCK = 48    |

---

## Configurazione dell'IDE Arduino

Le seguenti impostazioni nell'Arduino IDE funzionano per la maggior parte dei progetti del repository.

### Parametri di compilazione

| Parametro | Valore |
|-----------|--------|
| **USB CDC On Boot** | Disabled |
| **CPU Frequency** | 240MHz (WiFi) |
| **Core Debug Level** | None |
| **USB DFU On Boot** | Disabled |
| **Erase All Flash Before Sketch Upload** | Disabled |
| **Events Run On** | Core 1 |
| **Flash Mode** | QIO 80MHz |
| **Flash Size** | 4MB (32Mb) |
| **JTAG Adapter** | Disabled |
| **Arduino Runs On** | Core 1 |
| **USB Firmware MSC On Boot** | Disabled |
| **Partition Scheme** | Huge APP (3MB No OTA/1MB SPIFFS) |
| **PSRAM** | OPI PSRAM |
| **Upload Mode** | UART0 / Hardware CDC |
| **Upload Speed** | 921600 |
| **USB Mode** | Hardware CDC and JTAG |

> **Nota:** installare il supporto per ESP32 nell'Arduino IDE tramite il Board Manager prima di compilare gli sketch.

---

## Licenza

Questo progetto è distribuito con licenza **Creative Commons – Attribuzione – Non Commerciale 4.0 Internazionale (CC BY-NC 4.0)**.

Puoi condividerlo e modificarlo liberamente, **citando l’autore** (Davide Nasato / [davidegat](https://github.com/davidegat)) e **senza scopi commerciali**.

🔗 [https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)

---
