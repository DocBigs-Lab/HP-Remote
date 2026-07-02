[← zurück zur Übersicht](../README.md) · [Inbetriebnahme](SETUP.md) · [Debugging](DEBUG.md) · [MQTT-Beispiele](MQTT-EXAMPLES.md)

# Hardware & Montage

## Hardware

### ESP32-S3-CAM Board

| Eigenschaft | Wert |
|---|---|
| Board | ESP32-S3-CAM (Amazon B0F4D8ZY6L) |
| Kamera | OV2640 onboard |
| Flash | 16 MB QIO |
| PSRAM | 8 MB OPI |
| USB | OTG (links) für Betrieb, TTL (rechts) für erstes Flashen |

### Touch-Interface PCB

| Komponente | Package | Funktion |
|---|---|---|
| PCF8574TS | SSOP-20 | I2C 8-Bit Port-Expander, Adresse 0x20 |
| 2× TLP281-4 | SOP-16 | 4-fach Optokoppler (alle 8 Kanäle genutzt, 6 aktiv) |
| R1–R6: 330 Ω | 0603 | Vorwiderstände Optokoppler-LEDs |
| R7, R8: 4,7 kΩ | 0603 | I2C Pull-Ups SDA/SCL |
| C1: 100 nF | 0603 | Bypass-Kondensator VDD |
| CN1: JST-PH 2,0 mm | 4-polig | ESP-Seite: 3,3 V / SDA / SCL / GND_ESP |
| CN2: JST-PH 2,0 mm | 2-polig | WP-Seite: GND_WP (Display-Masse) |

> 💡 **Fertig aufgebaute Platinen können bei mir bezogen werden** – komplett bestückt und einsatzbereit. Bei Interesse einfach über GitHub melden.

---

## Kamera-Pinout (ESP32-S3-CAM, fest verdrahtet)

| Signal | GPIO |
|---|---|
| XCLK | 15 |
| SIOD / SIOC | 4 / 5 |
| VSYNC / HREF / PCLK | 6 / 7 / 13 |
| Y9–Y2 (D7–D0) | 16, 17, 18, 8, 9, 10, 11, 12 |

## I2C Touch-Interface Pinout

| Signal | GPIO | Funktion |
|---|---|---|
| SDA | 21 | I2C Daten → PCF8574 |
| SCL | 47 | I2C Takt → PCF8574 |

## Optionaler Heizraum-Sensor (SHT31, Temperatur/Luftfeuchte)

Zusätzlich zur Touch-Steuerung lässt sich optional ein **SHT31-Sensor** anschließen, der Temperatur und Luftfeuchte im Heizraum erfasst. Er teilt sich den vorhandenen I2C-Bus mit dem PCF8574 – es ist **kein zusätzliches Kabel und kein zusätzlicher GPIO** nötig.

| Eigenschaft | Wert |
|---|---|
| Sensor | SHT31 (I2C-Breakout, z. B. Adafruit/HiLetgo) |
| I2C-Adresse | `0x44` (Standardadresse, ADDR-Pin auf GND/offen) |
| Anschluss | parallel zum PCF8574 an SDA (GPIO 21) / SCL (GPIO 47), siehe unten |
| Versorgung | 3,3 V vom ESP32-S3-CAM |

Der Sensor ist **rein optional**: Ist keiner angeschlossen, läuft das Gerät unverändert weiter – keine Fehlermeldungen, keine zusätzlichen MQTT-Topics, keine Home-Assistant-Entities. Wird er später nachgerüstet, erkennt die Firmware ihn automatisch im laufenden Betrieb (kein Neustart nötig) und aktiviert MQTT/HA-Discovery dann selbstständig. Details zur Konfiguration und zum MQTT-Verhalten siehe [SETUP.md](SETUP.md#heizraum-sensor-optional-sht31).

> 💡 Da der SHT31 parallel zum PCF8574 am selben Bus hängt, reicht es, seine vier Pins (VCC, GND, SDA, SCL) an die entsprechenden Adern von CN1 anzulöten/abzugreifen – z. B. direkt an der Touch-PCB oder per kleinem Abzweig im Kabel zum ESP32.

## PCF8574 Port-Zuordnung

| Befehl | Port | Taste | Position |
|---|---|---|---|
| `power` | P7 | Power ⏻ | Oben links |
| `boost` | P6 | Boost ⚡ | Oben 2. v.l. |
| `settings` | P2 | Settings ⚙ | Oben 3. v.l. |
| `time` | P1 | Time 🕐 | Oben rechts |
| `up` | P5 | Temp+ ∧ | Unten (unter Boost) |
| `down` | P3 | Temp− ∨ | Unten (unter Settings) |

**Low-Active:** Port LOW = Optokoppler schaltet = Tastendruck simuliert

---

## Schaltungsprinzip

```
ESP32-S3-CAM
  GPIO21 (SDA) ──[4k7]── VCC
  GPIO47 (SCL) ──[4k7]── VCC
       │
  PCF8574TS (0x20)
  P0–P7 (Low-Active)
       │
  [330Ω]
       │
  TLP281-4 (LED-Seite)   ← GND_ESP
       │
  TLP281-4 (Transistor)
  Kollektor ──── Kontaktfläche (Touch-Pad)
  Emitter   ──── GND_WP (Display-Masse)
```

**Wichtig:** `GND_ESP` und `GND_WP` sind galvanisch getrennt – das ist der Vorteil der Optokoppler-Lösung.

---

## Montage & Aufbau

> **Hinweis:** Dieser Abschnitt ist ein Entwurf. Die Bild-Platzhalter (`docs/images/…`) werden ergänzt, sobald die Halterungen finalisiert sind.

Der mechanische Aufbau besteht aus zwei Baugruppen, die vor dem Display der Wärmepumpe sitzen: der **Kamera** (liest das Display optisch aus) und dem **Touch-Adapter** (der die Fronttasten simuliert). Beide werden jeweils über eine eigene Halterung am Gerät fixiert.

### 1. Touch-Adapter aufsetzen

Die Touch-Platine trägt sechs vergoldete Kontaktflächen, die exakt über den sechs kapazitiven Tasten der Wärmepumpe sitzen müssen.

- Die Kontaktflächen müssen **plan auf dem Display-Glas** aufliegen – schon ein kleiner Luftspalt verschlechtert die kapazitive Kopplung.
- Die Position jeder Kontaktfläche muss mittig über der zugehörigen Taste liegen (Layout: obere Reihe ⏻ ⚡ ⚙ 🕐, untere Reihe ∧ ∨).
- Anpressdruck gleichmäßig halten – die Halterung sollte die Platine sanft, aber sicher gegen das Glas drücken.

![Touch-Adapter auf dem Display](images/montage-touch.jpg)

### 2. Kamera positionieren

Die OV2640-Kamera muss das Display formatfüllend und möglichst frontal erfassen.

- Abstand so wählen, dass das Display den Bildausschnitt gut ausfüllt (Feinausrichtung später per Live-Stream).
- Die Kamera möglichst **parallel zur Displayebene** ausrichten – starke Schrägen erschweren die OCR. Kleine Restschiefe wird softwareseitig über die Feinrotation (`fine_rotation`) korrigiert.
- Auf gleichmäßige Sicht ohne Reflexionen achten (das selbstleuchtende Display ist unkritisch, aber Streulicht/Spiegelungen auf dem Glas vermeiden).

![Kamera-Position](images/montage-kamera.jpg)

### 3. Verkabelung

| Verbindung | Stecker | Adern |
|---|---|---|
| ESP ↔ Touch-PCB | CN1 (JST-PH 4-polig) | 3,3 V, SDA, SCL, GND_ESP |
| WP-Masse ↔ Touch-PCB | CN2 (JST-PH 2-polig) | GND_WP |

Die beiden Massen (`GND_ESP` und `GND_WP`) sind galvanisch getrennt und dürfen **nicht** verbunden werden – diese Trennung ist der Kern der Optokoppler-Lösung.

![Verkabelung](images/montage-verkabelung.jpg)

### 4. Erstinbetriebnahme der Touch-Funktion

Der kapazitive Controller der Wärmepumpe kalibriert beim Einschalten seine Grundkapazität. Sitzen die Kontaktflächen bereits auf, müssen sie in diese Kalibrierung einbezogen werden:

1. Touch-Adapter montieren (Kontaktflächen liegen auf dem Glas).
2. Wärmepumpe **aus- und wieder einschalten**.
3. Der Controller kalibriert die Kontaktflächen nun als Teil der Grundlast ein → die Tastensimulation funktioniert zuverlässig.

Wird dieser Schritt ausgelassen, kann es sein, dass Tastendrücke nicht erkannt werden, weil der Controller die Kontaktflächen bereits als „Dauerberührung" wegkalibriert hat.

### 5. Gehäuse & Befestigung

*(folgt – Halterungen werden noch finalisiert und anschließend bebildert.)*

![Fertige Montage](images/montage-komplett.jpg)

---

[← zurück zur Übersicht](../README.md) · [Inbetriebnahme](SETUP.md) · [Debugging](DEBUG.md) · [MQTT-Beispiele](MQTT-EXAMPLES.md)