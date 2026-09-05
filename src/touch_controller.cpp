#include "touch_controller.h"
#include <Wire.h>
#include "debug_log.h"

WpTouchController WpTouch;

// Aktueller Zustand aller 8 Ports (alle HIGH = inaktiv)
static uint8_t g_portState = 0xFF;

// PCF8574 erreichbar? Wird beim begin() geprüft.
static bool g_pcfAvailable = false;

// ─── PCF8574 schreiben ────────────────────────────────────────────────────────
static bool pcfWrite(uint8_t state) {
    if (!g_pcfAvailable) return false;   // Kein PCF → still überspringen
    Wire.beginTransmission(PCF8574_ADDR);
    Wire.write(state);
    uint8_t err = Wire.endTransmission();
    if (err != 0) {
        LOGE("PCF8574 write error: %d\n", err);
        return false;
    }
    g_portState = state;
    return true;
}

// ─── Einzelnen Port Low/High setzen ──────────────────────────────────────────
static bool pcfPinLow(uint8_t port) {
    return pcfWrite(g_portState & ~(1 << port));
}

static bool pcfPinHigh(uint8_t port) {
    return pcfWrite(g_portState | (1 << port));
}

// ─── Button-Name → Port-Nummer ───────────────────────────────────────────────
static int buttonToPort(const String &btn) {
    if (btn == "power")    return WP_PORT_POWER;
    if (btn == "boost")    return WP_PORT_BOOST;
    if (btn == "settings") return WP_PORT_SETTINGS;
    if (btn == "time")     return WP_PORT_TIMER;
    if (btn == "up")       return WP_PORT_UP;
    if (btn == "down")     return WP_PORT_DOWN;
    return -1;
}

// ─── Public API ───────────────────────────────────────────────────────────────
void WpTouchController::begin() {
    Wire.begin(WP_I2C_SDA, WP_I2C_SCL);
    Wire.setClock(100000);          // 100 kHz Standard-Mode
    Wire.setTimeOut(50);            // 50ms I2C-Timeout statt Default (kein Hängen)

    // ─── I2C Bus-Scan (Diagnose) – immer sichtbar, unabhängig von debug_level ──
    Serial.printf("\n==== I2C Bus-Scan (SDA=%d SCL=%d) ====\n", WP_I2C_SDA, WP_I2C_SCL);
    std::vector<uint8_t> devices = scanBus();
    for (uint8_t addr : devices) {
        Serial.printf("  I2C device found at 0x%02X%s\n", addr,
             (addr == PCF8574_ADDR) ? "  <-- PCF8574 (Touch)" : "");
    }
    if (devices.empty()) {
        Serial.println("  No I2C devices found (check wiring/power)");
    }
    Serial.printf("==== Scan done: %d device(s) ====\n", (int)devices.size());

    // PCF8574 gezielt prüfen
    Wire.beginTransmission(PCF8574_ADDR);
    uint8_t err = Wire.endTransmission();

    if (err == 0) {
        g_pcfAvailable = true;
        Serial.printf("PCF8574 ready at 0x%02X - Touch ENABLED\n", PCF8574_ADDR);
        pcfWrite(0xFF);             // Alle Ports HIGH (inaktiv)
    } else {
        g_pcfAvailable = false;
        Serial.printf("PCF8574 NOT found (err=%d) - Touch DISABLED, continuing\n", err);
    }
}

bool WpTouchController::isAvailable() {
    return g_pcfAvailable;
}

// Port dauerhaft LOW halten – für Multimeter-Messung am Optokoppler-Ausgang
bool WpTouchController::hold(const String &button) {
    int port = buttonToPort(button);
    if (port < 0) {
        Serial.printf("hold: unknown button %s\n", button.c_str());
        return false;
    }
    if (!g_pcfAvailable) {
        Serial.printf("hold: PCF8574 not present\n");
        return false;
    }
    Serial.printf("hold: %s (P%d) -> LOW (held)\n", button.c_str(), port);
    return pcfPinLow(port);
}

// Alle Ports wieder HIGH (loslassen)
void WpTouchController::releaseAll() {
    Serial.printf("releaseAll: all ports -> HIGH\n");
    pcfWrite(0xFF);
}

// Langer Tastendruck: eine Taste durationMs lang halten, dann loslassen
bool WpTouchController::longPress(const String &button, unsigned long durationMs) {
    int port = buttonToPort(button);
    if (port < 0) {
        LOGE("longPress: unknown button %s\n", button.c_str());
        return false;
    }
    if (!g_pcfAvailable) {
        LOGI("longPress '%s' ignored (PCF8574 not present)\n", button.c_str());
        return false;
    }
    LOGI("longPress: %s (P%d) for %lums\n", button.c_str(), port, durationMs);
    pcfPinLow(port);
    delay(durationMs);
    pcfPinHigh(port);
    return true;
}

// Tastenkombination: mehrere Tasten gleichzeitig durationMs lang drücken
bool WpTouchController::combo(const String buttons[], int len, unsigned long durationMs) {
    if (!g_pcfAvailable) {
        LOGI("combo ignored (PCF8574 not present)\n");
        return false;
    }
    if (len < 1) return false;

    // Maske aller beteiligten Ports berechnen
    uint8_t mask = 0;
    for (int i = 0; i < len; i++) {
        int port = buttonToPort(buttons[i]);
        if (port < 0) {
            LOGE("combo: unknown button %s\n", buttons[i].c_str());
            return false;
        }
        mask |= (1 << port);
    }

    LOGI("combo: %d buttons (mask 0x%02X) for %lums\n", len, mask, durationMs);

    // Alle beteiligten Ports gleichzeitig LOW
    pcfWrite(g_portState & ~mask);
    delay(durationMs);
    // Alle wieder HIGH
    pcfWrite(g_portState | mask);
    return true;
}

std::vector<uint8_t> WpTouchController::scanBus() {
    std::vector<uint8_t> found;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) found.push_back(addr);
    }
    return found;
}

bool WpTouchController::rescan() {
    // PCF8574 erneut suchen (z.B. nachdem Platine angeschlossen wurde)
    Wire.beginTransmission(PCF8574_ADDR);
    uint8_t err = Wire.endTransmission();
    if (err == 0) {
        g_pcfAvailable = true;
        LOGI("PCF8574 rescan: found at 0x%02X – Touch ENABLED\n", PCF8574_ADDR);
        pcfWrite(0xFF);
    } else {
        g_pcfAvailable = false;
        LOGE("PCF8574 rescan: NOT found (err=%d)\n", err);
    }
    return g_pcfAvailable;
}

bool WpTouchController::press(const String &button, int count) {
    int port = buttonToPort(button);
    if (port < 0) {
        LOGE("Unknown button: %s\n", button.c_str());
        return false;
    }
    if (!g_pcfAvailable) {
        LOGI("Touch '%s' ignored (PCF8574 not present)\n", button.c_str());
        return false;
    }

    for (int i = 0; i < count; i++) {
        if (i > 0) delay(WP_TOUCH_REPEAT_GAP);

        LOGI("Touch: %s (P%d) press %d/%d\n",
             button.c_str(), port, i+1, count);

        pcfPinLow(port);
        delay(WP_TOUCH_PULSE_MS);
        pcfPinHigh(port);

        if (i < count - 1) delay(WP_TOUCH_GAP_MS);
    }
    return true;
}

bool WpTouchController::pressSequence(const String buttons[], int len, int delayMs) {
    for (int i = 0; i < len; i++) {
        if (!press(buttons[i])) return false;
        if (i < len - 1) delay(delayMs);
    }
    return true;
}
