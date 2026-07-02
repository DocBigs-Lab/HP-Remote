#pragma once

#include <Arduino.h>
#include <vector>
#include "touch_config.h"

class WpTouchController {
public:
    void begin();
    bool press(const String &button, int count = 1);
    bool pressSequence(const String buttons[], int len, int delayMs = 300);
    bool rescan();          // I2C-Bus neu scannen, gibt true wenn PCF8574 gefunden
    bool isAvailable();     // true wenn PCF8574 erreichbar

    // Scannt den kompletten I2C-Bus (Adressen 1-126) und liefert alle
    // antwortenden Adressen – für Diagnose (z.B. /api/i2c/scan). Kennzeichnet
    // keine bekannten Geräte, das macht der Aufrufer anhand der Adresse.
    std::vector<uint8_t> scanBus();

    // Langer Tastendruck: eine Taste für durationMs gedrückt halten, dann loslassen
    bool longPress(const String &button, unsigned long durationMs);

    // Tastenkombination: mehrere Tasten gleichzeitig für durationMs drücken
    bool combo(const String buttons[], int len, unsigned long durationMs);

    // Test-Hilfen für Multimeter-Messung:
    bool hold(const String &button);   // Port dauerhaft LOW (gedrückt halten)
    void releaseAll();                 // Alle Ports HIGH (loslassen)
};

extern WpTouchController WpTouch;
