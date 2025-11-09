# ESP32-S3 Panel-4848S040 · Experiments & Fun
**Autore:** Davide Nasato (gat)

<img src="https://github.com/user-attachments/assets/fa71b786-9da7-488c-a8c5-b9a4eecb2e9e"
     alt="guition-esp32-s3-4848s040-connector"
     width="500" height="500">

---

## Descrizione

Questo repository raccoglie **esperimenti e strumenti software** per il **pannello ESP32-S3 Panel-4848S040**, che integra:

* microcontrollore **ESP32-S3**
* **display IPS 480×480** con controller **ST7701**
* **touch capacitivo GT911**
* **slot microSD** collegato via **FSPI**

Gli sketch qui presenti esplorano diverse funzioni del pannello: grafica RGB, interfacce touch, salvataggio su SD e controllo della retroilluminazione.

---

## ⚙️ Hardware supportato

**Scheda:** ESP32-S3 Panel-4848S040 [Documentazione HomeDing](https://homeding.github.io/boards/esp32s3/panel-4848S040.htm)

### Pin principali (configurazione standard)

| Funzione       | Pin                                        |
| -------------- | ------------------------------------------ |
| I²C Touch      | SDA = 19, SCL = 45                         |
| Backlight      | 38 (PWM LEDC)                              |
| SWSPI (ST7701) | CS = 39, SCK = 48, MOSI = 47               |
| RGB Panel      | DE = 18, VSYNC = 17, HSYNC = 16, PCLK = 21 |
| Canali R       | 11, 12, 13, 14, 0                          |
| Canali G       | 8, 20, 3, 46, 9, 10                        |
| Canali B       | 4, 5, 6, 7, 15                             |
| SD (FSPI)      | CS = 42, MOSI = 47, MISO = 41, SCK = 48    |

---

## Licenza

Questo progetto è distribuito con licenza
**Creative Commons – Attribuzione – Non Commerciale 4.0 Internazionale (CC BY-NC 4.0)**.

Puoi condividerlo e modificarlo liberamente, **citando l’autore**
(Davide Nasato / [davidegat](https://github.com/davidegat)) e **senza scopi commerciali**.

🔗 [https://creativecommons.org/licenses/by-nc/4.0/](https://creativecommons.org/licenses/by-nc/4.0/)

---
