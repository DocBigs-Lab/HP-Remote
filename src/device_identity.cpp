#include "device_identity.h"
#include <WiFi.h>
#include <esp_mac.h>

static String g_cachedUid;

// ─── Interne Helfer: MAC byteweise lesen (zuverlässig auf ESP32-S3) ──────────
static void readMac(uint8_t mac[6]) {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

String deviceMacAddress() {
    uint8_t mac[6];
    readMac(mac);
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

// Letzte 3 Bytes der MAC als 6-stelliger Hex-Suffix (z.B. "16fcd8")
String deviceMacSuffix() {
    uint8_t mac[6];
    readMac(mac);
    char buf[7];
    snprintf(buf, sizeof(buf), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    return String(buf);
}

// Volle MAC als 12-stellige UID (z.B. "dcb4d916fcd8")
String devicePersistentUid() {
    if (g_cachedUid.length() >= 12) return g_cachedUid;
    uint8_t mac[6];
    readMac(mac);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    g_cachedUid = String(buf);
    return g_cachedUid;
}

// ─── Default-Bezeichner – alle mit MAC-Suffix, einheitlich "hp_remote" ────────
String defaultDeviceId()      { return "hp_remote_" + deviceMacSuffix(); }
String defaultDeviceName()    { return "HP-Remote " + deviceMacSuffix(); }
String defaultHostname()      { return "hp-remote-" + deviceMacSuffix(); }
String defaultMqttTopicBase() { return "heatpump/" + defaultDeviceId(); }
