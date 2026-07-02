#include "room_sensor.h"
#include <Wire.h>
#include "debug_log.h"

RoomSensor RoomSensorSht31;

static uint8_t sht31Crc8(const uint8_t *data, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

void RoomSensor::begin() {
    // Wire.begin(WP_I2C_SDA, WP_I2C_SCL) läuft bereits über WpTouch.begin() –
    // der Sensor teilt sich denselben I2C-Bus, kein erneuter Wire.begin() nötig.
}

bool RoomSensor::read(float tempOffset, float humidityOffset, float &tempC, float &humidityPct) {
    // Single Shot, High Repeatability, ohne Clock-Stretching (0x2400) –
    // robuster auf dem ESP32-Wire-Treiber als die Clock-Stretching-Variante.
    Wire.beginTransmission(SHT31_ADDR);
    Wire.write(0x24);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        available_ = false;
        return false;
    }

    delay(20);  // Messzeit High-Repeatability (max. 15ms) + Sicherheitspuffer

    if (Wire.requestFrom((int)SHT31_ADDR, 6) != 6) {
        available_ = false;
        return false;
    }

    uint8_t buf[6];
    for (int i = 0; i < 6; i++) buf[i] = Wire.read();

    if (sht31Crc8(buf, 2) != buf[2] || sht31Crc8(buf + 3, 2) != buf[5]) {
        LOGE("SHT31: CRC-Fehler – Messung verworfen\n");
        available_ = false;
        return false;
    }

    uint16_t rawTemp = ((uint16_t)buf[0] << 8) | buf[1];
    uint16_t rawHum  = ((uint16_t)buf[3] << 8) | buf[4];

    tempC = -45.0f + 175.0f * ((float)rawTemp / 65535.0f) + tempOffset;
    humidityPct = 100.0f * ((float)rawHum / 65535.0f) + humidityOffset;
    if (humidityPct < 0.0f)   humidityPct = 0.0f;
    if (humidityPct > 100.0f) humidityPct = 100.0f;

    lastTempC_ = tempC;
    lastHumidityPct_ = humidityPct;
    available_ = true;
    return true;
}
