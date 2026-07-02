# HP-Remote

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](https://github.com/DocBigs-Lab/HP-Remote/blob/main/LICENSE)
[![Platform: ESP32-S3](https://img.shields.io/badge/platform-ESP32--S3-blue.svg)](https://www.espressif.com/en/products/socs/esp32-s3)
[![Framework: Arduino](https://img.shields.io/badge/framework-Arduino%20%2F%20PlatformIO-00979D.svg)](https://platformio.org/)
[![MQTT](https://img.shields.io/badge/protocol-MQTT-660066.svg)](docs/MQTT-EXAMPLES.md)
[![Home Assistant](https://img.shields.io/badge/Home%20Assistant-Auto--Discovery-41BDF5.svg)](docs/SETUP.md)
[![GitHub last commit](https://img.shields.io/github/last-commit/DocBigs-Lab/HP-Remote)](https://github.com/DocBigs-Lab/HP-Remote/commits/main)

**Vollständige bidirektionale Fernsteuerung für Bosch / Buderus Brauchwasser-Wärmepumpen**

Ein ESP32-S3-CAM liest das selbstleuchtende 7-Segment-Display optisch aus **und** simuliert alle 6 kapazitiven Fronttasten über eine PCB mit I2C-Touch-Interface. Dank Rückkopplung von OCR und Tastensteuerung lassen sich **Solltemperatur und Uhrzeit verifiziert einstellen** – auf Wunsch vollautomatisch durch Tastensperre und Standby hindurch. Die Anbindung läuft universell über **MQTT** – Messwerte und Tastensteuerung sind so in jede MQTT-fähige Umgebung integrierbar. Für Home Assistant gibt es zusätzlich **MQTT Auto-Discovery**, sodass alle Sensoren und Tasten dort ohne manuelle Konfiguration erscheinen.

Basiert auf dem [ESP32-Display-Reader](https://github.com/DocBig/ESP32-Display-Reader) von DocBig, portiert auf den ESP32-S3-CAM und um I2C Touch-Steuerung erweitert.

---

## Letzte Änderungen

Siehe [CHANGELOG.md](CHANGELOG.md) für die vollständige Historie.

---

## Kompatible Geräte

Getestet mit der Steuerung der folgenden baugleichen Brauchwasser-Wärmepumpen:

| Hersteller | Modell |
|---|---|
| Bosch | Compress 5000 DW – CS5001DW 200 / 200 C / 260 / 260 C |
| Buderus | WPT 200.4A / WPT 260.4A (baugleich) |

Andere Geräte mit identischem kapazitivem Bedienpanel (selbstleuchtendes 7-Segment-Display, 6 Touch-Tasten) lassen sich vermutlich ebenfalls anbinden, sind aber nicht getestet.

---

## Was das Projekt macht

| Funktion | Mechanismus |
|---|---|
| **Display auslesen** | OV2640-Kamera → OCR → MQTT (Set Temp, Water Temp, Symbole) |
| **Tasten drücken** | PCF8574 I2C → 6 × TLP281-4 Optokoppler → kapazitive Kontaktflächen auf PCB. Einzel-, Mehrfach-, Lang-Druck, Tastenkombinationen, Sequenzen und benannte Shortcuts (Standby, Gebläse, Tastensperre, Timer) |
| **Solltemperatur setzen** | Verifizierter Closed-Loop (`set_temp`): liest per OCR, regelt nach, trifft den Zielwert exakt. Grobsprung + Feinschliff für Tempo, optional vollautomatisch durch Tastensperre und Standby hindurch (`auto`) |
| **Uhrzeit setzen** | Verifiziertes Stellen der WP-Uhr (`set_time`) für Sommer-/Winterzeit oder Drift – Stunden und Minuten getrennt, kürzeste Richtung, mit Feintuning-Durchgängen |
| **Live-Stream** | MJPEG-Stream (Port 81) auf `/live` mit ausklappbarer Direktsteuerung: Tasten, Shortcuts und Temperatur-Stepper/Presets |
| **Home Assistant** | Optionale MQTT Auto-Discovery: Sensoren + 6 Button-Entities + Live-URL |
| **Konfiguration** | Webinterface im Browser, ROI-Editor, OTA-Updates |
| **Config-Storage** | NVS (Preferences) – kein Dateisystem nötig |

---

## Dokumentation

Die ausführliche Dokumentation ist in vier Bereiche aufgeteilt:

| Dokument | Inhalt |
|---|---|
| **[Hardware & Montage](docs/HARDWARE.md)** | Board, Touch-PCB, Pinouts, Schaltungsprinzip, Aufbau & Montage |
| **[Inbetriebnahme & Konfiguration](docs/SETUP.md)** | Flashen, WLAN/MQTT, ROIs einrichten, MQTT-API (inkl. Solltemperatur- & Uhrzeit-Closed-Loop mit `auto`), Home Assistant |
| **[Test & Debugging](docs/DEBUG.md)** | I2C-Scan, Multimeter-Test, OCR-Diagnose (Test Detection), REST-API |
| **[MQTT ohne Home Assistant](docs/MQTT-EXAMPLES.md)** | Direkte Steuerung per CLI, MQTT-Tools, Cron & Node-RED |

---

## 🚀 Web Installer

Firmware direkt im Browser flashen – kein Tool erforderlich:

👉 **[Web Installer starten](https://docbigs-lab.github.io/HP-Remote/)**

ESP32-S3-CAM per USB anschließen, „Install" klicken (Chrome / Edge).

---

## Projektstruktur

```
HP-Remote/
├── src/
│   ├── main.cpp                  ← Setup + Loop + Solltemperatur-Closed-Loop (set_temp/auto)
│   ├── touch_config.h            ← PCF8574 Adresse, I2C-Pins, Port-Zuordnung
│   ├── touch_controller.h/.cpp   ← I2C/PCF8574: press(), hold(), releaseAll(), rescan()
│   ├── camera_service.cpp        ← OV2640 Pinout ESP32-S3-CAM, MJPEG-Stream
│   ├── analyzer.cpp/.h           ← 7-Segment OCR
│   ├── mqtt_service.cpp/.h       ← MQTT + Touch-Callback + Shortcuts + Subscribe
│   ├── mqtt_ha_discovery.cpp/.h  ← HA Auto-Discovery: Sensoren, Buttons, Stream
│   ├── web_server.cpp/.h         ← Webinterface + REST API + MJPEG Port 81
│   ├── web_ui.h                  ← eingebettetes HTML/JS
│   ├── app_config.cpp/.h         ← Konfiguration + JSON
│   ├── storage_service.cpp/.h    ← NVS (Preferences)
│   ├── device_identity.cpp/.h    ← MAC-basierte Geräte-UID
│   ├── wifi_manager.cpp/.h       ← WiFi + AP-Setup
│   ├── ota_service.cpp/.h        ← OTA
│   └── debug_log.h               ← Log-Makros
├── docs/                          ← Dokumentation + kompilierte Binaries
│   ├── HARDWARE.md                ← Hardware & Montage
│   ├── SETUP.md                   ← Inbetriebnahme & Konfiguration
│   ├── DEBUG.md                   ← Test & Debugging
│   ├── MQTT-EXAMPLES.md           ← MQTT-Steuerung ohne Home Assistant
│   ├── images/                    ← Bilder für die Doku
│   ├── HP-Remote-merged.bin       ← Erstinstallation (USB, alle Partitionen)
│   └── HP-Remote-OTA.bin          ← OTA-Update (nur Applikation)
├── platformio.ini                ← Board: esp32-s3-devkitc-1, qio_opi PSRAM
├── partitions_ota.csv            ← OTA-Partitionstabelle
├── version_build.py              ← injiziert FW_VERSION + FW_BUILD
├── merge_bin.py                  ← erzeugt HP-Remote-OTA.bin + HP-Remote-merged.bin
└── README.md                     ← Übersicht (dieses Dokument)
```

---

## Lizenz

MIT – basiert auf [ESP32-Display-Reader](https://github.com/DocBig/ESP32-Display-Reader) von DocBig.