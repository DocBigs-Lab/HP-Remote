[← zurück zur Übersicht](../README.md) · [Hardware & Montage](HARDWARE.md) · [Inbetriebnahme](SETUP.md) · [MQTT-Beispiele](MQTT-EXAMPLES.md)

# Test & Debugging

## Test- & Debug-Funktionen

Für Inbetriebnahme und Fehlersuche – besonders nützlich beim Testen der Touch-Hardware ohne Oszilloskop (nur mit Multimeter).

### I2C-Bus-Scan

Beim Boot wird automatisch ein I2C-Scan ausgegeben (immer sichtbar im Serial-Monitor, unabhängig vom Debug-Level):

```
==== I2C Bus-Scan (SDA=21 SCL=47) ====
  I2C device found at 0x20  <-- PCF8574 (Touch)
==== Scan done: 1 device(s) ====
PCF8574 ready at 0x20 - Touch ENABLED
```

Wird der PCF8574 nicht gefunden, läuft das System trotzdem weiter (OCR/MQTT funktionieren), nur die Touch-Steuerung ist deaktiviert.

**Bus zur Laufzeit neu scannen** (z. B. nach Anschließen der Platine ohne Reboot):

```bash
curl -X POST http://<IP>/api/i2c/scan
# → {"ok":true,"pcf8574_found":true,"devices":[{"address":"0x20","name":"PCF8574 (Touch)"},{"address":"0x44","name":"SHT31 (Room)"}]}
```

`devices[]` listet alle am Bus antwortenden Adressen (nicht nur PCF8574/SHT31) – unbekannte Adressen erscheinen ohne `name`-Feld. Im Web-UI unter "Touch-Controller" gibt es dafür den Button "Scan I2C bus".

### Touch-Test mit dem Multimeter

Ein einzelner Tastendruck dauert nur ~80 ms – zu kurz zum Messen. Die `hold`/`release`-Endpoints halten einen Port **dauerhaft** auf LOW, sodass man in Ruhe am Optokoppler-Ausgang messen kann.

**Messpunkt:** Touch-Pad (Optokoppler-Kollektor) gegen `GND_WP`. Multimeter auf Durchgangsprüfung/Widerstand.

```bash
# Port dauerhaft schalten (Optokoppler leitet → niederohmig)
curl -X POST http://<IP>/api/touch/hold \
  -H "Content-Type: application/json" -d '{"button":"power"}'

# wieder loslassen (alle Ports HIGH → hochohmig)
curl -X POST http://<IP>/api/touch/release
```

**Ablauf pro Kanal:** `hold` absetzen → messen (muss niederohmig werden) → `release` → nächster Button.

Gültige Buttons: `power`, `boost`, `settings`, `time`, `up`, `down`.

> **Tipp:** Während der Touch-Messung **MQTT vorübergehend deaktivieren**. Dann läuft keine periodische OCR-Auswertung, die Web-Requests werden nicht blockiert und `hold`/`release` reagieren sofort.

### Tastendruck per API

```bash
# Einmal drücken
curl -X POST http://<IP>/api/touch \
  -H "Content-Type: application/json" -d '{"button":"up"}'

# Mehrfach (z. B. +5 °C)
curl -X POST http://<IP>/api/touch \
  -H "Content-Type: application/json" -d '{"button":"up","count":5}'

# Langer Druck (z. B. Settings 5 Sekunden für Parametermenü)
curl -X POST http://<IP>/api/touch \
  -H "Content-Type: application/json" -d '{"button":"settings","duration":5000}'

# Tastenkombination (z. B. Up+Down gleichzeitig 3 Sekunden)
curl -X POST http://<IP>/api/touch \
  -H "Content-Type: application/json" -d '{"combo":["up","down"],"duration":3000}'
```

### Test-Detection (OCR-Diagnose)

Im Webinterface liefert **Test Detection** ein JSON mit allen erkannten Werten und einem `debug`-Block pro ROI. Bei 7-Segment-ROIs werden pro Ziffer die Segment-Mittelwerte (`a_avg` … `g_avg`), Schwellwerte und das resultierende Bit-Muster angezeigt – ideal um Segment-Geometrie und Schwellwerte zu justieren.

Bei Symbolen mit Auto-Threshold wird zusätzlich der gemessene `contrast` und der `min_contrast` ausgegeben. Liegt der Kontrast darunter, gilt das Symbol als „aus" (verhindert Falsch-Positive bei gleichmäßig dunklen Flächen).

### REST-API Übersicht

| Endpoint | Methode | Funktion |
|---|---|---|
| `/api/config` | GET / POST | Konfiguration lesen / schreiben |
| `/api/config/export` | GET | Config als `HP-Remote-config.json` |
| `/api/config/import` | POST | Config importieren |
| `/api/snapshot` | GET | Einzelbild (Graustufen-JPEG) |
| `/api/test` | GET | Test Detection (OCR-Diagnose mit Debug) |
| `/api/touch` | POST | Tastendruck: `button`/`count`/`duration` oder `combo`/`sequence` |
| `/api/touch/hold` | POST | Port dauerhaft LOW (Multimeter-Test) |
| `/api/touch/release` | POST | Alle Ports HIGH |
| `/api/i2c/scan` | POST | I2C-Bus neu scannen |
| `/api/reboot` | POST | Gerät neustarten |
| `/api/wifi_reset` | POST | WLAN-Daten löschen, Neustart in Setup-AP |
| `/api/update` | POST | OTA-Firmware-Upload (Browser) |
| `/live` | GET | MJPEG-Stream + Touch-Buttons |

### Serial-Monitor Hinweis

Die `Serial.print`-Diagnose (I2C-Scan, Touch-Status) wird über den **TTL-USB-Port (rechts)** ausgegeben. Am OTG-Port (links) erscheint sie nicht. Der Boot-Log mit Firmware-Version und Build-Timestamp hilft zu prüfen ob die neueste Firmware läuft:

```
Booting HP-Remote
Firmware: 1.0.001
Build: 20260617-094017
```

---


---

[← zurück zur Übersicht](../README.md) · [Hardware & Montage](HARDWARE.md) · [Inbetriebnahme](SETUP.md) · [MQTT-Beispiele](MQTT-EXAMPLES.md)
