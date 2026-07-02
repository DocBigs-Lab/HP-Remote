#pragma once

#include <Arduino.h>

// A0=A1=GND -> 0x44 (Standard-Adresse der meisten SHT31-Breakouts)
#define SHT31_ADDR 0x44

// Optionaler SHT31-Raumsensor (I2C) für Heizraum-Temperatur/Luftfeuchte.
// Läuft am gemeinsamen I2C-Bus mit dem PCF8574-Touch-Controller
// (Wire.begin() erfolgt bereits über WpTouch.begin()).
class RoomSensor {
public:
    void begin();

    // Liest Temperatur/Luftfeuchte und wendet die Kalibrier-Offsets an.
    // Lesen und Verfügbarkeitsprüfung sind ein einziger Vorgang: schlägt die
    // I2C-Transaktion fehl, wird isAvailable() false und der Rückgabewert ist
    // false – der Aufrufer published in diesem Fall keine Werte/Discovery.
    bool read(float tempOffset, float humidityOffset, float &tempC, float &humidityPct);

    bool isAvailable() const { return available_; }
    float lastTempC() const { return lastTempC_; }
    float lastHumidityPct() const { return lastHumidityPct_; }

private:
    bool available_ = false;
    float lastTempC_ = 0.0f;
    float lastHumidityPct_ = 0.0f;
};

extern RoomSensor RoomSensorSht31;
