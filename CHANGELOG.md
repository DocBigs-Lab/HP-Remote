# Changelog

Diese Datei wird **manuell** gepflegt (kein Build-Skript, kein Auto-Generator) und listet nutzerrelevante Änderungen am HP-Remote. Neueste Einträge oben. Details siehe [Dokumentation](docs/SETUP.md).

## 2026-07-02
- Heizraum-Sensor wird jetzt unabhängig von MQTT kontinuierlich gelesen (Übertragung bleibt an aktives MQTT gekoppelt) – siehe [SETUP.md](docs/SETUP.md#heizraum-sensor-optional-sht31).
- I2C-Bus-Scan (`/api/i2c/scan`) listet jetzt alle gefundenen Geräte (inkl. SHT31), nicht nur den PCF8574 – neuer Button "Scan I2C bus" im Webinterface, siehe [DEBUG.md](docs/DEBUG.md).

## 2026-06-28
- Optionaler Heizraum-Sensor (Temperatur/Luftfeuchte, SHT31) hinzugefügt – siehe [SETUP.md](docs/SETUP.md#heizraum-sensor-optional-sht31).
