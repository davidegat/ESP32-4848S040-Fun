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

## English Version

# ESP32-S3 Panel-4848S040 · Experiments & Fun
**Author:** Davide Nasato (gat)

## Table of Contents

1. [Overview](#overview)
2. [Included Projects](#included-projects)
3. [Supported Hardware](#supported-hardware)
4. [Arduino IDE Setup](#arduino-ide-setup)
5. [License](#license)

---

## Overview

This repository gathers **experiments and software tools** for the **ESP32-S3 Panel-4848S040**, which integrates:

* **ESP32-S3 microcontroller**;
* **480×480 IPS display** with **ST7701** controller;
* **GT911 capacitive touch** panel;
* **microSD slot** connected via **FSPI**.

The sketches explore RGB graphics, touch interfaces, microSD storage, and backlight control, providing ready-to-adapt examples.

---

## Included Projects

* **`ESP32-S3-HelloWorld/`** – “Hello World” sample to quickly check the display and toolchain.
* **`Fotine/`** – Wi-Fi photo frame that periodically downloads images and shows them on the panel.
* **`NewsTicker/`** – News ticker powered by RSS feeds with Wi-Fi configuration through a captive portal.
* **`PartenzeCH/`** – Departure board for Swiss transportation, updating from public APIs with a touch interface.
* **`Pixxellata/`** – Pixel art editor featuring a touch palette and microSD saving in JPEG/BMP.
* **`QuadrantiOraQuadra/`** – Collection of watch faces compatible with the OraQuadra Nano v1.3 project.
* **`RandomYoutube/`** – Generator of random YouTube links and QR codes for display experiments on the panel.
* **`SquaredCoso/`** – Always-on dashboard that rotates themed pages (weather, calendar, transport, quotes, system stats) with a built-in `/settings` portal.
* **`Pongino/`** – Arkanoid-style game tailored for the 4848S040 panel with touch-controlled paddle, power-ups, and animated effects.

---

## Supported Hardware

**Board:** ESP32-S3 Panel-4848S040 — [HomeDing Documentation](https://homeding.github.io/boards/esp32s3/panel-4848S040.htm)

### Key pins (standard configuration)

| Function         | Pins                                       |
| ---------------- | ------------------------------------------- |
| I²C Touch        | SDA = 19, SCL = 45                          |
| Backlight        | 38 (PWM LEDC)                               |
| SWSPI (ST7701)   | CS = 39, SCK = 48, MOSI = 47                |
| RGB Panel        | DE = 18, VSYNC = 17, HSYNC = 16, PCLK = 21  |
| R Channels       | 11, 12, 13, 14, 0                           |
| G Channels       | 8, 20, 3, 46, 9, 10                         |
| B Channels       | 4, 5, 6, 7, 15                              |
| SD (FSPI)        | CS = 42, MOSI = 47, MISO = 41, SCK = 48     |

---

## Arduino IDE Setup

The following Arduino IDE settings work for most of the projects in this repository.

### Build Parameters

| Parameter | Value |
|-----------|-------|
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

> **Note:** install ESP32 support in the Arduino IDE through the Board Manager before compiling the sketches.

---

## License

This project is distributed under the **Creative Commons – Attribution – Non Commercial 4.0 International (CC BY-NC 4.0)** license.

You may share and adapt it freely, **crediting the author** (Davide Nasato / [davidegat](https://github.com/davidegat)) and **excluding commercial use**.

🔗 [https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)

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
* **`SquaredCoso/`** – dashboard always-on che alterna pagine tematiche (meteo, calendario, trasporti, citazioni, stato di sistema) configurabili dal portale `/settings`.
* **`Pongino/`** – clone di Arkanoid per il pannello 4848S040 con paddle touch, power-up e effetti animati.

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
