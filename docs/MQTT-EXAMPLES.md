[← zurück zur Übersicht](../README.md) · [Hardware & Montage](HARDWARE.md) · [Inbetriebnahme](SETUP.md) · [Debugging](DEBUG.md)

# MQTT-Steuerung ohne Home Assistant

HP-Remote ist **nicht** auf Home Assistant angewiesen. Die gesamte Kommunikation läuft über einfache MQTT-Topics – damit lässt sich das Gerät aus jeder MQTT-fähigen Umgebung heraus auslesen und steuern: per Kommandozeile, mit grafischen Tools, aus Skripten, Node-RED, ioBroker, openHAB usw.

Dieses Dokument zeigt die direkte Steuerung über MQTT-Bordmittel.

---

## Topic-Übersicht

Alle Topics leiten sich vom **Basis-Topic** ab. Dieses ist frei konfigurierbar; der Standard ist:

```
heatpump/hp_remote_XXXXXX/state
```

(`XXXXXX` = die letzten Stellen der MAC-Adresse). Daraus ergeben sich:

| Topic | Richtung | Inhalt |
|---|---|---|
| `<BASE>` | ESP → Welt | ausgelesene Display-Werte (JSON, retained) |
| `<BASE>/touch/cmd` | Welt → ESP | **Befehle** (Tastendrücke, Shortcuts) |
| `<BASE>/touch/state` | ESP → Welt | Rückmeldung zum letzten Befehl |
| `<BASE>/availability` | ESP → Welt | `online` / `offline` (LWT) |
| `<BASE>/health` | ESP → Welt | Health-/Statusdaten (JSON) |
| `<BASE>/snapshot` | ESP → Welt | aktuelles Kamerabild (JPEG, falls aktiviert) |

> **Das exakte Topic herausfinden:** Beim Verbinden gibt das Gerät im Serial-Log die abonnierte Befehlsadresse aus:
> ```
> MQTT subscribed: heatpump/hp_remote_a1b2c3/state/touch/cmd
> ```
> Genau auf dieses Topic werden die Befehle gesendet. Wer das Basis-Topic im Webinterface ändert, passt die Beispiele unten entsprechend an.

---

## 1. Befehle senden mit `mosquitto_pub`

Das Kommandozeilen-Tool `mosquitto_pub` (Teil der [Mosquitto](https://mosquitto.org/)-Tools) ist der direkteste Weg. Grundform:

```bash
mosquitto_pub -h <BROKER> -t '<BASE>/touch/cmd' -m '<JSON>'
```

Mit Authentifizierung:

```bash
mosquitto_pub -h <BROKER> -u <USER> -P <PASS> -t '<BASE>/touch/cmd' -m '<JSON>'
```

### Beispiele für alle Befehlsarten

Annahme: Broker `192.168.1.10`, Basis-Topic `heatpump/hp_remote_a1b2c3/state`.

**Einzelner Tastendruck** (Temperatur +1):

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"button":"up"}'
```

**Mehrfachdruck** (5× = +5 °C):

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"button":"up","count":5}'
```

> Die Firmware kompensiert automatisch den vom WP-Controller „verschluckten" ersten Druck: Bei `count ≥ 2` wird intern ein Druck zusätzlich gesendet, sodass `count:5` echte +5 ergibt.

**Langer Druck** (Settings 5 s, z. B. Parametermenü):

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"button":"settings","duration":5000}'
```

**Tastenkombination** (Up+Down gleichzeitig 3 s):

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"combo":["up","down"],"duration":3000}'
```

**Sequenz** (Tasten nacheinander):

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"sequence":["settings","up","up"]}'
```

**Benannter Shortcut** (z. B. Standby ein/aus):

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"shortcut":"standby"}'
```

Verfügbare Shortcuts: `standby`, `fan`, `lock`, `timer`, `params` (Details siehe [SETUP.md → Benannte Shortcuts](SETUP.md#mqtt-api)).

**Solltemperatur verifiziert setzen** (Closed-Loop mit OCR-Kontrolle):

```bash
# absolut: exakt auf 58 °C regeln
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"set_temp":58}'

# relativ verifiziert: +5 °C, mit OCR-Kontrolle des Ergebnisses
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"button":"up","count":5,"verify":true}'

# mit auto: bei aktiver Tastensperre selbst entsperren, einstellen, wieder sperren
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"set_temp":58,"auto":true}'
```

Das Gerät liest den Sollwert, drückt Richtung Ziel, kontrolliert per Kamera und regelt nach. Die Rückmeldung kommt auf `<BASE>/touch/state`, z. B. `{"ok":true,"set_temp":58,"value":58}` oder im Fehlerfall `{"ok":false,"error":"display_locked"}`. Mit `"auto":true` hebt das Gerät eine aktive Tastensperre selbst auf und setzt sie danach wieder (Antwort enthält dann `"auto_unlocked":true,"relocked":true`). Voraussetzung ist eine passende ROI-Benennung (Label enthält `Soll`/`Set`/`Target` bzw. `Ist`/`Water` und `Lock`) – siehe [SETUP.md](SETUP.md#solltemperatur-per-ocr-verifikation-closed-loop).

**Uhrzeit stellen** (Sommer-/Winterzeit, Drift-Korrektur):

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"set_time":"14:45"}'
```

Das Gerät liest die aktuelle Uhrzeit, stellt Stunden und Minuten getrennt ein (kürzeste Richtung) und kontrolliert das Ergebnis – bei Bedarf in bis zu 3 Durchgängen. Rückmeldung z. B. `{"ok":true,"set_time":"14:45","value":"14:45","passes":2}`. Voraussetzung ist ein Uhrzeit-ROI (Label enthält `Uhr`/`Zeit`/`Time`/`Clock`).

Mit `{"set_time":"14:45","auto":true}` erledigt das Gerät auch eine aktive Tastensperre und den Standby selbst (entsperren → aufwecken → stellen → einschlafen → sperren) – die Antwort enthält dann zusätzlich `"auto_unlocked":true,"relocked":true` bzw. `"auto_woken":true,"reslept":true`. Ein solcher Vorgang kann über eine Minute dauern; das Feedback kommt zuverlässig am Ende.

---

## 2. Werte & Rückmeldungen empfangen mit `mosquitto_sub`

**Display-Werte mitlesen** (die ausgelesenen Temperaturen, Symbole usw.):

```bash
mosquitto_sub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state'
```

Ausgabe (Beispiel):

```json
{"valid":true,"Ist-Temperatur":47,"Soll-Temperatur":60,"Uhrzeit-HP":732,"Run":true}
```

**Rückmeldung zum letzten Befehl** mitlesen:

```bash
mosquitto_sub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/state'
```

Ausgabe nach einem Tastendruck:

```json
{"last":"up","ok":true}
```

**Alles auf einmal beobachten** (Wildcard über alle Sub-Topics):

```bash
mosquitto_sub -h 192.168.1.10 -v \
  -t 'heatpump/hp_remote_a1b2c3/state/#'
```

Das `-v` zeigt zusätzlich das Topic vor jeder Nachricht – praktisch zum Debuggen.

---

## 3. Grafische MQTT-Tools

Wer nicht auf der Kommandozeile arbeiten möchte:

| Tool | Plattform | Eignung |
|---|---|---|
| **MQTT Explorer** | Windows/macOS/Linux | Topics als Baum durchklicken, Nachrichten senden/empfangen – ideal zum Erkunden |
| **MQTTX** | Windows/macOS/Linux | moderner Client, gut für gespeicherte Befehls-Vorlagen |
| **IoT MQTT Panel** | Android/iOS | Dashboard mit eigenen Buttons – z. B. ein Button pro Shortcut |
| **MQTT.fx** | Desktop | klassischer Allrounder |

**Tipp für Dashboard-Apps (z. B. IoT MQTT Panel):** Lege je einen Button an, der auf `<BASE>/touch/cmd` publiziert, mit fester Payload wie `{"shortcut":"standby"}`. So baust du dir in wenigen Minuten eine eigene Fernbedienung ohne jegliche Smart-Home-Plattform.

---

## 4. Automatisierung

### Cron (zeitgesteuert)

Beispiel: Jeden Abend um 22:00 Uhr in den Standby schalten, morgens um 5:00 Uhr wieder aktivieren. In der Crontab (`crontab -e`):

```cron
# Wärmepumpe abends in Standby
0 22 * * * mosquitto_pub -h 192.168.1.10 -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' -m '{"shortcut":"standby"}'

# morgens wieder aus dem Standby
0  5 * * * mosquitto_pub -h 192.168.1.10 -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' -m '{"shortcut":"standby"}'
```

> Hinweis: `standby` ist ein Umschalter (toggle). Es lohnt sich, vorher den tatsächlichen Zustand über das Display-Topic zu prüfen, statt blind zu schalten.

### Shell-Skript mit Zustandsprüfung

```bash
#!/usr/bin/env bash
BROKER=192.168.1.10
BASE=heatpump/hp_remote_a1b2c3/state

# aktuellen Zustand einmalig lesen (-C 1 = nach 1 Nachricht beenden)
STATE=$(mosquitto_sub -h "$BROKER" -t "$BASE" -C 1)
echo "Aktueller Status: $STATE"

# Beispiel: nur hochheizen wenn Ist-Temp unter 45 °C
IST=$(echo "$STATE" | grep -o '"Ist-Temperatur":[0-9]*' | grep -o '[0-9]*')
if [ "$IST" -lt 45 ]; then
  mosquitto_pub -h "$BROKER" -t "$BASE/touch/cmd" -m '{"button":"up","count":3}'
  echo "Solltemperatur erhöht."
fi
```

### Node-RED

Ein minimaler Flow zum Senden eines Shortcuts:

1. **Inject-Node** (löst manuell oder per Zeitplan aus)
2. **Function-** oder **Change-Node**, setzt `msg.payload`:
   ```json
   {"shortcut":"fan"}
   ```
3. **MQTT-out-Node**, Topic `heatpump/hp_remote_a1b2c3/state/touch/cmd`

Zum Anzeigen der Display-Werte:

1. **MQTT-in-Node**, Topic `heatpump/hp_remote_a1b2c3/state`
2. **JSON-Node** (parst die Payload)
3. **Debug-** oder **Dashboard-Node** (z. B. Gauge auf `msg.payload["Ist-Temperatur"]`)

### Andere Plattformen

Da alles über Standard-MQTT läuft, funktionieren **ioBroker** (mqtt-Adapter), **openHAB** (MQTT Binding), **FHEM** und eigene Python-Skripte (`paho-mqtt`) nach demselben Muster: auf `<BASE>` lauschen, an `<BASE>/touch/cmd` senden.

Beispiel mit Python (`paho-mqtt`):

```python
import paho.mqtt.publish as publish

publish.single(
    topic="heatpump/hp_remote_a1b2c3/state/touch/cmd",
    payload='{"shortcut":"timer"}',
    hostname="192.168.1.10",
)
```

---

## Hinweis: MQTT muss aktiv sein

Damit das Gerät Befehle annimmt **und** Werte sendet, muss **MQTT im Webinterface aktiviert** sein. Bei deaktiviertem MQTT bleibt das Gerät ruhig (siehe [SETUP.md](SETUP.md)). Die periodische Display-Auswertung läuft ebenfalls nur bei aktivem MQTT – die Tastensteuerung per `/touch/cmd` reagiert jedoch unabhängig davon, sobald eine MQTT-Verbindung besteht.

## Hinweis: Tastensperre muss aus sein

Die Wärmepumpe führt Tastenbefehle nur aus, wenn die **Tastensperre nicht aktiv** ist. Bei verriegeltem Display (Locked-Symbol leuchtet) bleiben alle `/touch/cmd`-Befehle wirkungslos. Den Status kann man am `Locked`-Wert im Display-Topic ablesen; entsperren bzw. umschalten lässt sich per Shortcut:

```bash
mosquitto_pub -h 192.168.1.10 \
  -t 'heatpump/hp_remote_a1b2c3/state/touch/cmd' \
  -m '{"shortcut":"lock"}'
```

---

[← zurück zur Übersicht](../README.md) · [Hardware & Montage](HARDWARE.md) · [Inbetriebnahme](SETUP.md) · [Debugging](DEBUG.md)
