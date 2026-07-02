[← zurück zur Übersicht](../README.md) · [Hardware & Montage](HARDWARE.md) · [Debugging](DEBUG.md) · [MQTT-Beispiele](MQTT-EXAMPLES.md)

# Inbetriebnahme & Konfiguration

## Software – Installation

### 🚀 Web Installer (einfachste Methode)

Firmware direkt im Browser flashen – kein Tool, keine Toolchain nötig:

👉 **[Web Installer starten](https://docbigs-lab.github.io/HP-Remote/)**

ESP32-S3-CAM per USB anschließen, auf „Install" klicken, fertig. Funktioniert in **Chrome** oder **Edge** (Web Serial). Danach weiter bei [WLAN einrichten](#3-wlan-einrichten).

Wer selbst kompilieren oder per Kommandozeile flashen möchte, nutzt die folgenden Schritte.

### 1. Projekt öffnen

```bash
# Archiv entpacken
tar -xzf HP-Remote.tar.gz
# In VS Code öffnen: File → Open Folder → HP-Remote
```

### 2. Erstmalig flashen (USB TTL-Port, rechter USB-C)

```bash
platformio run --environment xiao_esp32s3 --target upload
```

Alternativ – ohne selbst zu kompilieren – die fertige `docs/HP-Remote-merged.bin` per esptool oder ESP Web Tools an Adresse `0x0` flashen (enthält Bootloader, Partitionstabelle und Applikation).

### 3. WLAN einrichten

Nach dem ersten Start öffnet der ESP einen Access Point:

```
SSID: HP-Remote-Setup
IP:   192.168.4.1
```

Mit diesem AP verbinden, im Browser `192.168.4.1` öffnen und hier **nur die WLAN-Zugangsdaten** (Netzwerk + Passwort, optional Hostname) eintragen → **Save WiFi**. Der ESP verbindet sich danach mit dem Heimnetz.

> **WLAN später wechseln:** Bei einem bereits eingerichteten Gerät im Webinterface unter **Configuration → Reset WiFi (Setup AP)** klicken. Das Gerät löscht die WLAN-Zugangsdaten, startet neu und öffnet wieder den Setup-AP `HP-Remote-Setup`. Alle anderen Einstellungen (MQTT, ROIs) bleiben erhalten. Anschließend wie oben mit dem neuen Netz verbinden.

### 4. MQTT & Gerät konfigurieren

Sobald der ESP im Heimnetz hängt, das Webinterface unter seiner neuen IP (oder `hp-remote.local`) öffnen und dort **MQTT-Broker, Gerätename und ROIs** einstellen → **SaveConfig**.

> **Wichtig – MQTT steuert die Datenerfassung:**
> Die periodische Display-Auswertung (OCR) läuft **nur wenn MQTT aktiviert ist**. Bei deaktiviertem MQTT bleibt das Gerät ruhig und erfasst/überträgt keine Daten – Webinterface, Live-Stream und Touch-Steuerung funktionieren weiterhin.
>
> **Tipp für die Touch-Bedienung:** Zum Einstellen der Wärmepumpe über den Remote-Touch (z. B. Menüs, Parameter, Tastenkombinationen) **MQTT vorübergehend deaktivieren**. Dann blockiert die OCR-Auswertung nicht die Web-Requests, und die Tastenbefehle reagieren sofort und störungsfrei. Nach dem Einstellen MQTT wieder aktivieren.

### 5. Weitere Updates per OTA

Nach der Ersteinrichtung lassen sich Firmware-Updates bequem über das Webinterface einspielen – kein USB-Kabel nötig:

1. Im Browser das Gerät öffnen (`http://<IP>/`) und die Sektion **OTA Update** aufklappen.
2. Die Datei `HP-Remote-OTA.bin` (wird beim Build automatisch im `docs/`-Ordner erzeugt) auswählen und hochladen.
3. Das Gerät flasht und startet selbstständig neu. Nach dem Reboot zeigt der Boot-Log die neue Build-Nummer (`Build: …`).

Alternativ per Kommandozeile:

```bash
platformio run --environment xiao_esp32s3 --target upload \
  --upload-port hp-remote-XXXXXX.local
```

---

## Kamera einstellen

Bevor ROIs angelegt werden, sollte die Kamera ein **scharfes, gut belichtetes und gerades** Bild des Displays liefern. Die Einstellungen finden sich im Webinterface in der **Camera**-Sektion; gespeichert werden sie als Teil der Konfiguration (NVS).

Das selbstleuchtende Display ist im Grunde eine kleine Lichtquelle vor dunklem Hintergrund – die Belichtung ist daher der wichtigste Hebel: zu hell und die Segmente „blühen" aus und verschmelzen, zu dunkel und die OCR findet keine Kanten.

**Bewährtes Referenz-Setting** (funktioniert an einer Bosch/Buderus-WP zuverlässig – als Startpunkt gedacht, je nach Einbau und Umgebungslicht anzupassen):

```json
"camera": {
  "width": 800,
  "height": 600,
  "jpeg_quality": 20,
  "auto_exposure": true,
  "auto_gain": true,
  "auto_whitebalance": true,
  "aec_value": 200,
  "agc_gain": 0,
  "vflip": true,
  "hflip": true,
  "fine_rotation": 1
}
```

**Die Parameter im Einzelnen:**

| Parameter | Bedeutung |
|---|---|
| `width` / `height` | Auflösung des Kamerabilds. 800×600 (SVGA) ist ein guter Kompromiss aus Detail und Tempo; höher kostet RAM/Zeit, ohne die OCR spürbar zu verbessern. |
| `jpeg_quality` | JPEG-Kompression (0 = beste Qualität/größte Datei … 63 = stark komprimiert). Niedrige Werte (hier 20) halten die Ziffernkanten scharf. |
| `auto_exposure` | Automatische Belichtung. Mit `true` regelt der Sensor selbst; `aec_value` dient dann als Zielwert/Bias. |
| `aec_value` | Belichtungs-Sollwert. Der **entscheidende** Wert für ein selbstleuchtendes Display – höher = heller. 200 ist ein guter Startpunkt; bei ausgeblühten Segmenten senken, bei zu dunklem Bild erhöhen. |
| `auto_gain` / `agc_gain` | Automatische Verstärkung. `agc_gain` 0 hält das Rauschen niedrig (wichtig, da das Display ohnehin hell genug ist). |
| `auto_whitebalance` | Automatischer Weißabgleich – unkritisch, da nur Helligkeitskanten ausgewertet werden, kann aber an bleiben. |
| `vflip` / `hflip` | Bild vertikal/horizontal spiegeln. Je nach Einbaulage der Kamera nötig, damit das Display richtig herum erscheint. |
| `fine_rotation` | Feinkorrektur der Bilddrehung in Grad, um eine leichte Schieflage auszugleichen (per Slider im Webinterface). |

> 💡 **Vorgehen:** Erst `vflip`/`hflip` so setzen, dass das Display richtig herum steht, dann mit **Capture** ein Standbild prüfen und `aec_value` justieren, bis die Ziffern klar und ohne Ausblühen erscheinen. Zuletzt mit `fine_rotation` die Ziffern exakt waagerecht stellen. Erst dann mit dem Anlegen der ROIs beginnen.

---

## Display einrichten – ROIs anlegen & konfigurieren

Damit Werte über MQTT rausgehen, muss dem Gerät beigebracht werden **wo** auf dem Display welche Information steht. Das geschieht über **ROIs** (Regions of Interest) – rechteckige Bereiche, die einer Ziffernanzeige oder einem Symbol zugeordnet werden. Die Konfiguration läuft komplett im Webinterface.

### Grundprinzip

Es gibt zwei ROI-Typen:
- **Sevenseg** – eine 7-Segment-Zahl (z. B. Temperatur, Uhrzeit). Liefert einen Zahlenwert.
- **Symbol** – ein Icon das an/aus ist (z. B. Boost, Betriebsanzeige). Liefert `true`/`false`.

### Schritt für Schritt

**1. Kamera ausrichten & Bild aufnehmen**

Die Kamera so positionieren dass das Display formatfüllend und möglichst gerade im Bild ist (Kamera-Parameter wie Belichtung und Spiegelung siehe Abschnitt [Kamera einstellen](#kamera-einstellen) oben). Im Webinterface über dem Kamerabild auf **Capture** klicken – es erscheint ein Standbild. Bei leichter Schieflage den **Fine Rotation**-Slider nutzen, bis die Ziffern waagerecht stehen.

**2. ROI anlegen**

Über dem Bild auf **+ Sevenseg** (für Zahlen) oder **+ Symbol** (für Icons) klicken. Ein neues Rechteck erscheint. Mit der Maus:
- in die Mitte ziehen → verschieben
- an den Ecken ziehen → Größe ändern

Das Rechteck so platzieren dass es **genau** eine Zifferngruppe (z. B. die zweistellige Solltemperatur) bzw. ein Symbol umschließt.

**3. ROI konfigurieren (Sektion ROI Settings)**

| Feld | Bedeutung |
|---|---|
| **ID** | interner, eindeutiger Bezeichner (stabil – wird für MQTT/HA genutzt) |
| **Label** | Klartext-Name (erscheint in Test Detection, z. B. „Soll-Temperatur") |
| **Type** | `sevenseg` oder `symbol` |
| **Digits** | (nur sevenseg) Anzahl der Ziffern, z. B. 2 für Temperatur, 4 für Uhrzeit |
| **Decimal places** | (nur sevenseg) Nachkommastellen |
| **Auto Threshold** | (nur sevenseg) automatische Schwellwert-Findung pro Ziffer – für schwach leuchtende Anzeigen empfehlenswert |
| **Seg Profile** | Segment-Geometrie-Vorlage (siehe unten) |

**4. Segment-Profil justieren (nur sevenseg)**

7-Segment-Ziffern werden anhand von 7 Segment-Positionen (a–g) ausgewertet. Über dem Capture-Fenster die Checkbox **Segments** aktivieren – dann werden die einzelnen Segmente im Bild eingeblendet. In der Sektion **Segment/Profiles Editor** lassen sich die Positionen (rx/ry/rw/rh in % der Ziffer) anpassen, bis jedes farbige Segment-Rechteck **mittig auf dem zugehörigen LED-Balken** sitzt. Für unterschiedliche Schriftgrößen (z. B. große Temperatur vs. kleine Uhrzeit) eigene Profile anlegen.

**5. MQTT/HA-Ausgabe festlegen (Sektion HA / MQTT)**

| Feld | Bedeutung |
|---|---|
| **HA Enabled** | ROI wird per Auto-Discovery als HA-Entität angelegt |
| **HA Name** | Anzeigename in Home Assistant |
| **HA Unit** | Einheit, z. B. `°C` |
| **HA Device Class** | z. B. `temperature` – steuert Icon/Darstellung in HA |
| **HA State Class** | z. B. `measurement` – für Langzeit-Statistik in HA |

**Im einfachsten Fall genügt es, nur den HA-Namen zu vergeben** – Einheit, Device Class und Icon werden anhand des Namens automatisch erkannt (enthält der Name z. B. „Temp"/„Temperatur", werden `°C` und `temperature` gesetzt). Die übrigen Felder müssen nur ausgefüllt werden, wenn die automatische Erkennung nicht passt oder fehlt.

Auch ohne HA werden die Werte über MQTT veröffentlicht – die HA-Felder steuern nur die zusätzliche Auto-Discovery.

**6. Testen**

Auf **Test Detection** klicken. Das Ergebnis ist ein JSON mit drei Feldern auf oberster Ebene – `ok`, `result` und `debug`:

```json
{
  "ok": true,
  "result": {
    "valid": true,
    "Ist-Temperatur": 47,
    "Soll-Temperatur": 60,
    "Uhrzeit-HP": 732,
    "Boost": false,
    "Run": true,
    "Ready": false
  },
  "debug": {
    "symbols":  [ … ],
    "sevenseg": [ … ]
  }
}
```

- **`ok`** – ob die Anfrage technisch erfolgreich war (Bild aufgenommen und ausgewertet). Sagt noch nichts über die Erkennungsqualität aus.
- **`result`** – die fertig erkannten Werte je ROI (nach Label benannt). `valid` ist `true`, wenn alle ROIs ein gültiges Ergebnis lieferten. Ein Wert von `-1` bei einem Sevenseg-ROI bedeutet „nicht erkannt".
- **`debug`** – Detaildaten zur Fehlersuche, getrennt nach `symbols` und `sevenseg` (die Einträge werden unten erklärt).

**Sevenseg-Debug** (pro Ziffer d0, d1, …):

**Sevenseg-Debug** – pro ROI gibt es allgemeine Felder und je Ziffer einen Block `d0`, `d1`, …:

| Feld | Ebene | Bedeutung |
|---|---|---|
| `seg_profile` | ROI | verwendetes Segment-Profil |
| `geometry_ok` | ROI | ob die ROI groß genug für die Ziffernzahl ist |
| `digit_w` | ROI | berechnete Breite einer Ziffer in Pixeln |
| `otsu_threshold` | ROI | (nur bei Auto-Threshold) global ermittelter Schwellwert |
| `a`…`g` | Ziffer | erkannter Zustand jedes Segments (1 = an, 0 = aus) |
| `a_avg`…`g_avg` | Ziffer | gemessener Helligkeitswert je Segment (0–255) |
| `threshold` | Ziffer | Schwellwert, ab dem ein Segment als „an" gilt |
| `gap_threshold`/`gap_max` | Ziffer | (bei Auto-Threshold) automatisch gefundene Trennschwelle und größter Helligkeitssprung |
| `stretch_min`/`stretch_max` | Ziffer | (bei aktiviertem Stretch-Contrast) Helligkeits-Spreizung |
| `bits` | Ziffer | resultierendes Bitmuster der 7 Segmente |
| `value` | Ziffer | die dekodierte Ziffer (`-1` = ungültiges Muster) |

**Beispiel – eine erkannte „7"** (Ziffer `d1` der Uhrzeit „0732"):

```json
"d1": {
  "segments": {
    "a": 1,
    "b": 1,
    "c": 1,
    "d": 0,
    "e": 0,
    "f": 0,
    "g": 0,
    "a_avg": 206,
    "b_avg": 171,
    "c_avg": 153,
    "d_avg": 19,
    "e_avg": 6,
    "f_avg": 18,
    "g_avg": 11,
    "threshold": 50,
    "invert_logic": false,
    "bits": 7
  },
  "value": 7
}
```

Lesart: Die Segmente **a, b, c** (oben + rechte Seite) sind hell (153–206, über `threshold` 50) → „an", alle anderen dunkel (6–19). Das Bitmuster `bits: 7` (= a+b+c) entspricht einer **7** → `value: 7`. Wäre z. B. `c_avg` niedrig obwohl das Segment leuchtet, würde `c:0` gesetzt, das Muster ungültig und `value: -1` – ein klares Zeichen, dass das c-Segment im Profil nachjustiert werden muss.

> Hinweis: Bei aktiviertem **Auto Threshold** erscheinen zusätzlich `gap_threshold`/`gap_max` und bei **Stretch-Contrast** `stretch_min`/`stretch_max` im `segments`-Block (siehe Tabelle oben).

**Symbol-Debug:**

| Feld | Bedeutung |
|---|---|
| `avg` | mittlere Helligkeit der Symbol-Fläche (0–255) |
| `state` | erkannter Zustand (`true`/`false`) |
| `auto_threshold` | ob automatische Schwellwert-Findung aktiv ist (bei Symbolen i. d. R. `false`) |
| `threshold_on`/`threshold_off` | Ein-/Ausschalt-Schwellen (feste Schwellwerte) |
| `invert_logic` | ob dunkel = „an" gilt |
| `contrast`/`min_contrast` | (nur bei Auto-Threshold) gemessener Kontrast vs. Mindestwert |

**Beispiel – ein leuchtendes Symbol** (Run, aktiv):

```json
{
  "id": "Run",
  "avg": 56,
  "state": true,
  "auto_threshold": false,
  "threshold_on": 40,
  "threshold_off": 35,
  "invert_logic": false
}
```

Lesart: Die Symbol-Fläche ist hell genug (`avg` 56), liegt über der Einschaltschwelle (`threshold_on` 40) → `state: true`, das Symbol leuchtet. Ein ausgeschaltetes Symbol (z. B. Boost) hätte hier ein niedriges `avg` (etwa 16–21) unter `threshold_off` → `state: false`. Die zwei Schwellen (`on` 40 / `off` 35) bilden eine Hysterese, damit der Zustand bei Werten knapp an der Grenze nicht flackert.

> **Tipp – bei Symbolen ist weniger oft mehr:** Das ROI muss **nicht** das ganze Symbol umfassen. Da `avg` über die gesamte ROI-Fläche gemittelt wird, „verwässern" große dunkle Bereiche eines filigranen Symbols den Mittelwert – der Hell-Dunkel-Unterschied zwischen an und aus wird klein und die Schwellen liegen eng beieinander. Besser ein **kleines ROI auf einen kompakten, immer mitleuchtenden Teil** des Symbols legen (z. B. den dicksten Balken oder Kern). Dann ist der `avg`-Sprung zwischen an und aus groß und eindeutig, und die Erkennung wird robust.

Stimmen die Werte nicht:
- Sevenseg: ROI-Position und Segment-Profil prüfen (sitzen die Segmente auf den LEDs?), ggf. **Auto Threshold** aktivieren
- Symbol: `threshold_on`/`threshold_off` anpassen
- Details zum Debug-Block siehe [Test & Debugging → Test-Detection](DEBUG.md#test-detection-ocr-diagnose)

**7. Speichern & MQTT aktivieren**

Mit **SaveConfig** alles dauerhaft speichern. Sobald **MQTT aktiviert** ist (MQTT-Sektion), beginnt das Gerät im eingestellten `capture_interval_sec`-Takt die Werte zu erfassen und an den Broker zu senden. In Home Assistant erscheinen die Entitäten automatisch (sofern HA Enabled gesetzt war).

> **Tipp:** ROIs während der Einrichtung über die Checkbox **🔒 ROI locked** sperren, sobald sie sitzen – das verhindert versehentliches Verschieben beim weiteren Justieren. ROIs werden ohnehin am gecaptureten Standbild eingestellt; nur beim **Ausrichten der Kamera** über den Live-Stream lohnt es sich, MQTT vorübergehend zu deaktivieren (flüssigeres Bild, siehe oben).

---

## Heizraum-Sensor (optional, SHT31)

Zusätzlich zur Display-Auswertung kann das Gerät optional Temperatur und Luftfeuchte im Heizraum erfassen – über einen SHT31-Sensor, der sich den I2C-Bus mit dem Touch-Controller teilt (siehe [Hardware & Montage](HARDWARE.md#optionaler-heizraum-sensor-sht31-temperaturluftfeuchte)). Diese Funktion ist **vollständig optional** und erfordert keine Code- oder Konfigurationsänderung, um sie ungenutzt zu lassen.

### Automatische Erkennung

- Ist kein Sensor angeschlossen, bleibt das Feature unsichtbar: keine MQTT-Werte, keine Home-Assistant-Entities, keine Fehlermeldung.
- Wird ein Sensor angeschlossen (auch nachträglich, im laufenden Betrieb), erkennt die Firmware ihn beim nächsten Lese-Intervall automatisch – **kein Neustart nötig**. Ab diesem Moment erscheinen die beiden Entitäten in Home Assistant und die Werte werden per MQTT veröffentlicht.
- Fällt der Sensor später aus (Kabelbruch, abgezogen), wird er ebenso automatisch wieder als „nicht verfügbar" erkannt – die MQTT-Availability-Topic geht auf `offline`, Home Assistant markiert die Entitäten entsprechend, ohne dass veraltete Werte stehen bleiben.
- Das Auslesen läuft **unabhängig von MQTT** kontinuierlich im eingestellten Intervall (siehe unten) – auch bei deaktiviertem MQTT. Nur die **Übertragung** (State/Discovery/Availability) setzt aktives MQTT voraus.
- Den aktuellen Status zeigt die Status-Leiste im Webinterface (**Room Sensor**, **Room Temperature**, **Room Humidity**) – dort erscheinen live Werte auch ohne aktives MQTT.

### Einstellungen (Webinterface, Sektion „Room Sensor")

| Einstellung | Bedeutung |
|---|---|
| **Enable** | Schaltet die Erfassung komplett ab (z. B. um den Sensor vorübergehend stillzulegen, ohne ihn abzuklemmen). Bei Deaktivierung wird die Availability sofort auf `offline` gesetzt. |
| **Temp Offset (°C)** | Kalibrier-Korrektur für die gemessene Temperatur (z. B. Eigenerwärmung des Sensors durch Elektronik in der Nähe ausgleichen). Bereich ±10 °C. |
| **Humidity Offset (%)** | Kalibrier-Korrektur für die gemessene Luftfeuchte. Bereich ±20 %. |
| **Read Interval (s)** | Wie oft der Sensor gelesen wird (läuft unabhängig von MQTT) – bei aktivem MQTT wird im selben Turnus auch übertragen. Default 60 s – Raumklima ändert sich langsam, ein schnelleres Intervall bringt keinen Mehrwert. Bereich 5–3600 s. |

### MQTT-Topics

```
Topic:   <BASE>/room/state
Payload: {"room_temp_c": 21.4, "room_humidity_pct": 47.8}

Topic:   <BASE>/room/availability
Payload: online | offline   (retained)
```

Die Availability läuft bewusst über eine **eigene** Topic statt der geräteweiten – so zeigt Home Assistant den Sensor unabhängig vom MQTT-Verbindungsstatus des Geräts als „nicht verfügbar", falls nur der Sensor selbst ausfällt.

### Home Assistant

Sobald der Sensor erkannt ist, registrieren sich automatisch zwei zusätzliche Sensor-Entities:

| Entity | device_class | Einheit |
|---|---|---|
| HP Heizraum Temperatur | `temperature` | °C |
| HP Heizraum Luftfeuchte | `humidity` | % |

---

## MQTT API

**Basis-Topic:** frei konfigurierbar, z. B. `heatpump/hp_remote_XXXXXX`

### Display-Werte (ESP32 → HA)

```
Topic:   heatpump/hp_remote_XXXXXX
Payload: {"set_temp": 55, "water_temp": 40, "valid": true}
```

### Touch-Steuerung (HA → ESP32)

```
Topic: heatpump/hp_remote_XXXXXX/touch/cmd
```

| Modus | Payload | Beschreibung |
|---|---|---|
| Einzel-Druck | `{"button": "up"}` | einmal drücken |
| Mehrfach-Druck | `{"button": "up", "count": 5}` | 5× drücken (+5 °C) |
| Langer Druck | `{"button": "settings", "duration": 5000}` | Taste 5 s halten (Menü-/Parameter-Funktionen) |
| Tastenkombination | `{"combo": ["up", "down"], "duration": 3000}` | mehrere Tasten gleichzeitig 3 s (z. B. Tastensperre) |
| Sequenz | `{"sequence": ["settings", "up", "up"]}` | Tasten nacheinander |
| Shortcut | `{"shortcut": "standby"}` | benannte Gerätefunktion (siehe unten) |
| **Solltemperatur (absolut)** | `{"set_temp": 58}` | regelt per OCR-Verifikation exakt auf 58 °C |
| **Solltemperatur (relativ, verifiziert)** | `{"button":"up","count":5,"verify":true}` | +5 °C mit OCR-Kontrolle des Ergebnisses |
| **Uhrzeit** | `{"set_time": "14:45"}` | stellt die Uhr per OCR-Verifikation (Sommer-/Winterzeit, Drift); optional `"auto":true` durch Sperre/Standby |

**Gültige Button-Namen:** `power`, `boost`, `settings`, `time`, `up`, `down`

**Benannte Shortcuts** (lösen den passenden Lang-Druck bzw. die Kombination aus):

| Shortcut | Funktion | entspricht |
|---|---|---|
| `standby` | Standby ein/aus | `power` ~3 s halten |
| `fan` | Gebläselüftung ein/aus | `boost` ~5 s halten |
| `lock` | Tastensperre ein/aus | `up`+`down` gleichzeitig ~5 s |
| `timer` | Timer-Einstellungen | `time` ~5 s halten |
| `params` | Parametereinstellungs-Oberfläche (im Standby) | `boost`+`settings` gleichzeitig ~5 s |

Diese Shortcuts sind auch als Buttons im Webinterface (Sektion **Touch-Controller**) verfügbar. Weitere Funktionen lassen sich bei Bedarf ergänzen.

### Solltemperatur per OCR-Verifikation (Closed-Loop)

Statt „blind" eine Anzahl Tastendrücke zu senden, kann das Gerät die Solltemperatur **verifiziert** einstellen: Es liest den aktuellen Wert per OCR, drückt Richtung Ziel, liest nach und regelt nach, bis der Zielwert exakt erreicht ist. Das ist robust gegen verschluckte Tastendrücke und Endanschläge.

Bei größeren Differenzen (ab 3 °C) arbeitet die Regelung zweistufig: Zunächst bringt ein **Grobsprung** per Mehrfachdruck-Burst den Wert in einem Rutsch nahe ans Ziel (Differenz − 1 °C), danach erfolgt der **verifizierte Feinschliff** in Einzelschritten. Das beschleunigt große Sprünge erheblich, ohne die Präzision zu verlieren – der Feinschliff korrigiert auch ein eventuelles Überschießen.

```
{"set_temp": 58}                              # absolut auf 58 °C
{"button":"up","count":5,"verify":true}       # relativ +5 °C, verifiziert
{"button":"down","count":3,"verify":true}     # relativ −3 °C, verifiziert
```

> ⚠️ **Voraussetzung – ROI-Benennung:** Diese Funktion findet die nötigen Anzeigen **über das ROI-Label** (unabhängig von Home Assistant). Damit sie funktioniert, müssen die ROIs entsprechend benannt sein:
> - **Solltemperatur:** Label enthält `Soll`, `Set` oder `Target` (z. B. „Soll-Temperatur")
> - **Ist-Temperatur** (für die Standby-Erkennung): Label enthält `Ist`, `Water` oder `Actual` (z. B. „Ist-Temperatur")
> - **Tastensperre** (für den Lock-Check): Label enthält `Lock` (z. B. „Locked")

**Rückmeldungen** (auf `<BASE>/touch/state`):

| Antwort | Bedeutung |
|---|---|
| `{"ok":true,"set_temp":58,"value":58}` | Zielwert erreicht |
| `{"ok":false,"error":"display_locked"}` | Tastensperre aktiv – erst entsperren |
| `{"ok":false,"error":"standby"}` | WP im Standby (Soll nicht verstellbar) |
| `{"ok":false,"error":"no_soll_roi"}` | kein passend benanntes Soll-ROI gefunden |
| `{"ok":false,"error":"no_change","value":X}` | Wert ändert sich nicht (Endanschlag / reagiert nicht) |
| `{"ok":false,"error":"max_iterations","value":X}` | Sicherheitslimit erreicht, Ziel nicht getroffen |

Der Lock-Check läuft bei `set_temp`/`verify` **immer** automatisch vorab.

#### Automatisches Entsperren (`"auto": true`)

Mit dem Flag `"auto": true` darf das Gerät eine aktive **Tastensperre selbstständig aufheben** und/oder die WP **aus dem Standby aufwecken**, die Temperatur einstellen und danach den **Ausgangszustand wiederherstellen** (wieder einschlafen, wieder sperren) – jeder Zustandswechsel wird per OCR verifiziert (max. 2 Versuche).

```
{"set_temp": 58, "auto": true}
{"button":"up","count":5,"verify":true,"auto":true}
```

Ohne `auto` bleibt das bisherige Verhalten (Abbruch mit `display_locked` bzw. `standby`).

Der Ablauf ist: **entsperren → aufwecken → einstellen → wieder einschlafen → wieder sperren**. Die Wiederherstellung erfolgt in dieser Reihenfolge, weil sich die Tastensperre an diesem WP-Modell auch im Standby noch setzen lässt – so blockiert die gesetzte Sperre nicht das Einschlafen.

| Zusätzliche Antwort-Felder / Fehler | Bedeutung |
|---|---|
| `…,"auto_unlocked":true,"relocked":true` | Sperre aufgehoben, eingestellt, wieder gesetzt |
| `…,"auto_unlocked":true,"relocked":false` | ⚠️ Wieder-Sperren fehlgeschlagen – Display bleibt entsperrt |
| `…,"auto_woken":true,"reslept":true` | aus Standby geweckt, eingestellt, wieder eingeschlafen |
| `…,"auto_woken":true,"reslept":false` | ⚠️ Wieder-Einschlafen fehlgeschlagen – WP bleibt wach |
| `{"ok":false,"error":"unlock_failed"}` | Entsperren nicht möglich (Zustand nicht erreicht) |
| `{"ok":false,"error":"wake_failed"}` | Aufwecken aus Standby nicht möglich |
| `{"ok":false,"error":"no_lock_roi"}` | `auto` verlangt ein „Locked"-ROI zur Verifikation |

Ein kompletter Auto-Vorgang kann mehrere Zustandswechsel umfassen (jeder ~3–5 s plus Verifikation); bei Sperre **und** Standby dauert es entsprechend **bis zu ~20–25 Sekunden**; das Gerät ist während dieser Zeit blockiert.

### Uhrzeit per OCR-Verifikation setzen

Auch die Uhr der Wärmepumpe lässt sich verifiziert stellen – gedacht für die **Sommer-/Winterzeit-Umstellung** oder gelegentliche Korrektur einer Drift von wenigen Minuten.

```
{"set_time": "14:45"}
{"set_time": "14:45", "auto": true}
```

Der Ablauf: Das Gerät liest die aktuelle Uhrzeit (stabiler Normalmodus), setzt dann **blind** Stunden und Minuten getrennt (Time → Stunden ⌃⌄ → Time → Minuten ⌃⌄ → Time) und kontrolliert anschließend das Ergebnis. Stimmt es nicht exakt, folgt ein Feintuning-Durchgang – **bis zu 3 Durchgänge**. Stunden und Minuten werden zyklisch in der **kürzesten Richtung** angefahren (z. B. 3 → 2 Uhr per einem Schritt runter, 58 → 02 min per vier Schritten hoch).

Die Uhr lässt sich nur im **wachen, entsperrten** Zustand stellen. Ohne `auto` bricht der Befehl bei Tastensperre bzw. Standby ab (`display_locked` / `standby`). Mit `"auto": true` übernimmt das Gerät – genau wie bei der Solltemperatur – das komplette Handling: **entsperren → aufwecken → Uhrzeit stellen → wieder einschlafen → wieder sperren**, jeder Zustandswechsel verifiziert.

> ⚠️ **Voraussetzung – ROI-Benennung:** Es muss ein Uhrzeit-ROI vorhanden sein, dessen Label `Uhr`, `Zeit`, `Time` oder `Clock` enthält (z. B. „Uhrzeit-HP"). Das ROI liest die Anzeige als `HHMM` (z. B. 1445).

**Rückmeldungen** (auf `<BASE>/touch/state`):

| Antwort | Bedeutung |
|---|---|
| `{"ok":true,"set_time":"14:45","value":"14:45","passes":2}` | Uhrzeit erreicht (nach 2 Durchgängen) |
| `…,"auto_unlocked":true,"relocked":true` | (mit `auto`) Sperre aufgehoben, gestellt, wieder gesetzt |
| `…,"auto_woken":true,"reslept":true` | (mit `auto`) aus Standby geweckt, gestellt, wieder eingeschlafen |
| `{"ok":false,"error":"not_reached","value":"14:44"}` | nach 3 Durchgängen nicht exakt getroffen |
| `{"ok":false,"error":"display_locked"}` | Tastensperre aktiv (ohne `auto`) |
| `{"ok":false,"error":"standby"}` | WP im Standby (ohne `auto`) |
| `{"ok":false,"error":"no_time_roi"}` | kein passend benanntes Uhrzeit-ROI gefunden |
| `{"ok":false,"error":"time_unreadable"}` | Uhrzeit nicht lesbar |
| `{"ok":false,"error":"bad_format"}` | Eingabe nicht im Format `HH:MM` |

Da der Einstellmodus blinkt (und dort schlecht lesbar ist), wird **nur vor und nach** dem Setzen gelesen, im Einstellmodus selbst „blind" gedrückt – das Feintuning gleicht verschluckte Schritte zuverlässig aus. `passes` gibt an, wie viele Durchgänge nötig waren.

> ⏱️ **Dauer:** Mit `auto` und mehreren Durchgängen kann ein Vorgang **über eine Minute** dauern (entsperren + aufwecken + bis zu 3 Stell-Durchgänge + einschlafen + sperren). Das Gerät ist währenddessen blockiert; die Rückmeldung kommt zuverlässig am Ende (die MQTT-Verbindung wird vor dem Senden bei Bedarf wiederhergestellt). Für die typischen Anwendungsfälle (Sommer-/Winterzeit = 1 h, kleine Drift) sind es nur ein bis zwei kurze Durchgänge.

**Hinweise zum Touch-Verhalten:**
- Der kapazitive Controller der Wärmepumpe registriert einen Tastendruck erst beim **Loslassen** (fallende Flanke). Ein einzelner `press` hält die Taste daher ~300 ms (`WP_TOUCH_PULSE_MS`) bevor er loslässt.
- `duration` ist die Haltezeit in Millisekunden. Fehlt sie bei `combo`, gilt ein Default von 500 ms.
- Bei `combo` werden alle beteiligten Ports über eine gemeinsame Bitmaske **gleichzeitig** geschaltet (ein einziger I2C-Schreibvorgang), nicht nacheinander.
- **Reagiert eine Taste oder Kombination nicht zuverlässig, die Haltezeit (`duration` bzw. `WP_TOUCH_PULSE_MS`) schrittweise um je 100 ms erhöhen.** Manche Funktionen brauchen eine etwas längere Mindest-Berührdauer, bis der Controller sie als gültig erkennt.
- **Mehrfachdruck-Kompensation:** Der WP-Controller verschluckt beim Mehrfachdruck zuverlässig den **ersten** Tastendruck (er dient nur zum „Aufwecken" der Eingabe). Die Firmware sendet bei `count ≥ 2` daher automatisch einen Druck zusätzlich, sodass `count:5` tatsächlich +5 ergibt. Beim Einzeldruck (`count:1`) wird **nicht** kompensiert.

> ⚠️ **Wichtige Voraussetzung – Tastensperre:** Tastenbefehle werden von der Wärmepumpe nur ausgeführt, wenn die **Tastensperre nicht aktiv** ist. Bei verriegeltem Display (Locked-Symbol an) laufen alle Touch-Befehle wirkungslos ins Leere. Die Sperre lässt sich per Shortcut `{"shortcut":"lock"}` umschalten – vor automatisierten Temperatureinstellungen also sicherstellen, dass das Display entsperrt ist.

### Rückmeldung (ESP32 → HA)

```
Topic:   heatpump/hp_remote_XXXXXX/touch/state
Payload: {"last": "up", "ok": true}
```

### Live-Stream URL

```
Topic:   heatpump/hp_remote_XXXXXX/live_url
Payload: http://172.16.0.198/live   (retained)
```

---

## Live-Seite (`/live`) – Stream + Direktsteuerung

Die Seite `http://<geräte-ip>/live` zeigt den Kamerastream und bietet darunter eine kompakte Bedienleiste. Sie eignet sich, um die WP direkt vom Handy oder Browser zu steuern, ohne MQTT-Tool oder Home Assistant.

**Standardansicht (platzsparend):**
- **Grundtasten:** Power, Boost, Settings, Time sowie Temp ⌃ / ⌄
- Zwei **Ausklapp-Schalter** an den Rändern der ersten Zeile: ⚡▾ (Shortcuts) und 🌡▾ (Temperatur)

**Ausklappbereich Shortcuts (⚡▾):** Standby, Gebläse, Tastensperre, Timer und Parametermenü – dieselben benannten Shortcuts wie über MQTT.

**Ausklappbereich Temperatur (🌡▾):**
- **Stepper** `−` / `+` mit Anzeige des Zielwerts; beim Aufklappen wird der **aktuelle Sollwert** automatisch geladen
- **Presets** 50° / 55° / 60° als Schnellwahl
- **Set** löst die verifizierte Einstellung aus (Closed-Loop mit Grobsprung + Feinschliff)

Während die Einstellung läuft, zeigt die Statusanzeige **⏳ bitte warten…**; am Ende erscheint der erreichte Wert (**✓ 55°**) bzw. eine Fehlerursache (**✕ locked**, **✕ standby** …). Da im Live-Popup ohnehin Sichtkontakt zum Display besteht, nutzt die Set-Funktion bewusst **kein** Auto-Handling – ist das Display gesperrt oder im Standby, wird das gemeldet und man entsperrt/weckt kurz selbst per Shortcut.

---

## Home Assistant

Nach dem ersten MQTT-Connect erscheinen automatisch:

- **Sensoren:** ROI-Werte (Set Temp, Water Temp, …) + Snapshot-Bild
- **6 Buttons:** Power, Boost, Settings, Time, Temp+, Temp−
- **Live-URL Sensor:** Link zur `/live` Seite mit Stream + Buttons
- **Heizraum-Sensoren (optional):** Temperatur + Luftfeuchte – erscheinen automatisch nur, wenn ein SHT31-Sensor angeschlossen ist, siehe [Heizraum-Sensor](#heizraum-sensor-optional-sht31)

### Live-Stream im Dashboard

```yaml
type: iframe
url: http://172.16.0.198/live
aspect_ratio: 75%
```

### Beispiel-Automation: PV-Überschuss → Boost

```yaml
automation:
  - alias: "WP Boost bei PV-Überschuss"
    trigger:
      - platform: numeric_state
        entity_id: sensor.pv_surplus_watt
        above: 1500
        for: "00:05:00"
    action:
      - service: mqtt.publish
        data:
          topic: "heatpump/hp_remote_XXXXXX/touch/cmd"
          payload: '{"button": "boost"}'
```

---


---

[← zurück zur Übersicht](../README.md) · [Hardware & Montage](HARDWARE.md) · [Debugging](DEBUG.md) · [MQTT-Beispiele](MQTT-EXAMPLES.md)