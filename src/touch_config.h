#pragma once

#include <Arduino.h>

// ─── PCF8574 I2C Adresse ──────────────────────────────────────────────────────
// A0=A1=A2=GND → 0x20
#define PCF8574_ADDR    0x20

// ─── I2C Pins (ESP32-S3-CAM) ─────────────────────────────────────────────────
#define WP_I2C_SDA      21
#define WP_I2C_SCL      47

// ─── Port-Zuordnung PCF8574 → Taste ──────────────────────────────────────────
// Obere Reihe (links→rechts): P1  P2  P5  P6
// Untere Reihe:               P3(unter P2)  P4(unter P5)
//
// PCF8574 ist Low-Active: Pin LOW = Optokoppler schaltet = Tastendruck
#define WP_PORT_POWER    7   // P1 – ⏻  oben links
#define WP_PORT_BOOST    6   // P2 – ⚡  oben 2. v.l.
#define WP_PORT_SETTINGS 2   // P5 – ⚙   oben 3. v.l.
#define WP_PORT_TIMER    1   // P6 – 🕐  oben rechts
#define WP_PORT_UP       5   // P3 – ∧   unten (unter Boost/P2)
#define WP_PORT_DOWN     3   // P4 – ∨   unten (unter Settings/P5)

// ─── Touch-Impuls Timing ─────────────────────────────────────────────────────
#define WP_TOUCH_PULSE_MS   300
#define WP_TOUCH_GAP_MS     200
#define WP_TOUCH_REPEAT_GAP 400

// ─── MQTT Topics ─────────────────────────────────────────────────────────────
#define WP_MQTT_TOPIC_CMD_SUFFIX   "/touch/cmd"
#define WP_MQTT_TOPIC_STATE_SUFFIX "/touch/state"