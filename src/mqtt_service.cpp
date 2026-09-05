#include "mqtt_service.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "debug_log.h"
#include "mqtt_ha_discovery.h"
#include "device_identity.h"
#include "touch_controller.h"
#include "touch_config.h"
#include "room_sensor.h"

static AppConfig*    g_cfgRef     = nullptr;
static PubSubClient* g_mqttClient = nullptr;

// Ergebnis des zuletzt verarbeiteten Touch-Befehls (z. B. set_temp-Closed-Loop).
// Wird von /api/touch ausgelesen, um dem Browser das echte Resultat zu liefern.
String g_lastTouchResult = "{\"ok\":true}";

// In main.cpp implementiert (hat Zugriff auf Kamera + OCR). Closed-Loop, der
// die Solltemperatur per OCR-Verifikation auf target einstellt.
// Ex-Varianten mit autoHandle: entsperren/sperren automatisch.
extern bool setSollTemperatureEx(int target, bool autoHandle, String &resultJson);
extern bool setSollRelativeEx(int delta, bool autoHandle, String &resultJson);
// Uhrzeit setzen (Closed-Loop): {"set_time":"14:45","auto":true}
extern bool setClockTimeStr(const String &hhmm, bool autoHandle, String &resultJson);

// Vorwärtsdeklaration (Definition weiter unten in dieser Datei).
bool mqttEnsureConnected(PubSubClient &mqtt, const AppConfig &cfg);

// Veröffentlicht ein Touch-Ergebnis auf <topic>/touch/state. Nutzt
// mqttEnsureConnected, das bei Bedarf reconnectet – wichtig nach langen
// Closed-Loop-/Auto-Vorgängen (set_temp/set_time), bei denen die MQTT-Schleife
// minutenlang nicht lief und die Verbindung getrennt sein kann.
static void publishTouchState(const String &payload) {
    if (!g_cfgRef || !g_mqttClient) return;
    if (!mqttEnsureConnected(*g_mqttClient, *g_cfgRef)) {
        LOGE("touch-state publish skipped: no MQTT connection\n");
        return;
    }
    String st = g_cfgRef->mqtt.topic + WP_MQTT_TOPIC_STATE_SUFFIX;
    g_mqttClient->publish(st.c_str(), payload.c_str(), false);
}

// ─── Touch MQTT Callback ──────────────────────────────────────────────────────
void mqttTouchCallback(const char* topic, byte* payload, unsigned int len) {
    char buf[384];
    if (len >= sizeof(buf)) return;
    memcpy(buf, payload, len);
    buf[len] = '\0';

    LOGI("WpTouch MQTT cmd: %s\n", buf);

    JsonDocument doc;
    if (deserializeJson(doc, buf) != DeserializationError::Ok) {
        LOGE("WpTouch: JSON parse error\n");
        return;
    }

    bool ok = false;
    g_lastTouchResult = "{\"ok\":true}";   // Default, von set_temp/verify überschrieben

    // ── Solltemperatur setzen (Closed-Loop): {"set_temp":58,"auto":true} ─────
    if (doc["set_temp"].is<int>()) {
        int target = doc["set_temp"].as<int>();
        bool autoHandle = doc["auto"].is<bool>() && doc["auto"].as<bool>();
        String resultJson;
        bool r = setSollTemperatureEx(target, autoHandle, resultJson);
        g_lastTouchResult = resultJson;
        publishTouchState(resultJson);
        LOGI("set_temp(%d, auto=%d) -> %s\n", target, autoHandle, resultJson.c_str());
        (void)r;
        return;
    }

    // ── Uhrzeit setzen (Closed-Loop): {"set_time":"14:45","auto":true} ───────
    if (doc["set_time"].is<String>() || doc["set_time"].is<const char*>()) {
        String hhmm = doc["set_time"].as<String>();
        bool autoHandle = doc["auto"].is<bool>() && doc["auto"].as<bool>();
        String resultJson;
        setClockTimeStr(hhmm, autoHandle, resultJson);
        g_lastTouchResult = resultJson;
        publishTouchState(resultJson);
        LOGI("set_time(%s, auto=%d) -> %s\n", hhmm.c_str(), autoHandle, resultJson.c_str());
        return;
    }

    // ── Benannte Shortcuts: {"shortcut":"standby"} ───────────────────────────
    // Übersetzt einen Funktionsnamen in den passenden Tasten-/Combo-Befehl
    // (aus der Bosch/Buderus-Bedienungsanleitung).
    if (doc["shortcut"].is<String>()) {
        String sc = doc["shortcut"].as<String>();
        sc.toLowerCase();
        bool r = false;
        String label = sc;

        if (sc == "standby") {
            // Standby ein/aus: Power ~3 s halten (mit Sicherheitspuffer)
            r = WpTouch.longPress("power", 3300);
        } else if (sc == "fan" || sc == "geblaese" || sc == "gebläse") {
            // Gebläselüftung ein/aus: Boost ~5 s halten (mit Sicherheitspuffer)
            r = WpTouch.longPress("boost", 5300);
        } else if (sc == "lock" || sc == "tastensperre") {
            // Tastensperre ein/aus: Up+Down gleichzeitig ~5 s (mit Sicherheitspuffer)
            String combo[2] = { "up", "down" };
            r = WpTouch.combo(combo, 2, 5300);
        } else if (sc == "timer" || sc == "timer_settings") {
            // Timer-Einstellungen: Time ~5 s halten (mit Sicherheitspuffer)
            r = WpTouch.longPress("time", 5300);
        } else if (sc == "params" || sc == "parameter" || sc == "menu") {
            // Parametereinstellungs-Oberfläche: Boost+Settings gleichzeitig ~5 s
            // (laut WP-Anleitung im Standby-Betrieb aufzurufen).
            String combo[2] = { "boost", "settings" };
            r = WpTouch.combo(combo, 2, 5300);
        } else {
            LOGE("WpTouch: unknown shortcut '%s'\n", sc.c_str());
            return;
        }

        if (g_cfgRef && g_mqttClient && g_mqttClient->connected()) {
            String st = g_cfgRef->mqtt.topic + WP_MQTT_TOPIC_STATE_SUFFIX;
            String pl = String("{\"last\":\"shortcut:") + label + "\",\"ok\":" +
                        (r ? "true" : "false") + "}";
            g_mqttClient->publish(st.c_str(), pl.c_str(), false);
        }
        return;
    }

    // ── Tastenkombination: {"combo":["up","down"],"duration":3000} ────────────
    if (doc["combo"].is<JsonArray>()) {
        JsonArray arr = doc["combo"].as<JsonArray>();
        uint8_t n = arr.size();
        if (n == 0 || n > 8) return;
        String btns[8];
        for (uint8_t i = 0; i < n; i++)
            btns[i] = arr[i].as<String>();
        unsigned long dur = doc["duration"] | 500UL;   // Default 500ms
        ok = WpTouch.combo(btns, n, dur);
        if (g_cfgRef && g_mqttClient && g_mqttClient->connected()) {
            String st = g_cfgRef->mqtt.topic + WP_MQTT_TOPIC_STATE_SUFFIX;
            String pl = String("{\"last\":\"combo\",\"ok\":") + (ok ? "true" : "false") + "}";
            g_mqttClient->publish(st.c_str(), pl.c_str(), false);
        }
        return;
    }

    // ── Tastensequenz: {"sequence":["settings","up"]} ─────────────────────────
    if (doc["sequence"].is<JsonArray>()) {
        JsonArray arr = doc["sequence"].as<JsonArray>();
        uint8_t n = arr.size();
        if (n == 0 || n > 16) return;
        String seq[16];
        for (uint8_t i = 0; i < n; i++)
            seq[i] = arr[i].as<String>();
        ok = WpTouch.pressSequence(seq, n);
        if (g_cfgRef && g_mqttClient && g_mqttClient->connected()) {
            String st = g_cfgRef->mqtt.topic + WP_MQTT_TOPIC_STATE_SUFFIX;
            String pl = ok ? "{\"last\":\"sequence\",\"ok\":true}"
                           : "{\"last\":\"sequence\",\"ok\":false}";
            g_mqttClient->publish(st.c_str(), pl.c_str(), false);
        }
        return;
    }

    if (!doc["button"].is<String>()) return;
    String btnName = doc["button"].as<String>();

    bool r;
    // ── Langer Tastendruck: {"button":"settings","duration":5000} ─────────────
    if (!doc["duration"].isNull()) {
        unsigned long dur = doc["duration"].as<unsigned long>();
        r = WpTouch.longPress(btnName, dur);
    } else {
        // Normaler (Mehrfach-)Druck: {"button":"up","count":N}
        uint8_t count = doc["count"] | 1;

        // ── Relativ mit Verifikation: {"button":"up","count":5,"verify":true} ──
        // Nutzt den OCR-Closed-Loop, um die tatsächliche Wertänderung zu
        // verifizieren (statt blind zu drücken). Nur für up/down sinnvoll.
        bool verify = doc["verify"].is<bool>() && doc["verify"].as<bool>();
        if (verify && (btnName == "up" || btnName == "down")) {
            int delta = (btnName == "up") ? (int)count : -(int)count;
            bool autoHandle = doc["auto"].is<bool>() && doc["auto"].as<bool>();
            String resultJson;
            setSollRelativeEx(delta, autoHandle, resultJson);
            g_lastTouchResult = resultJson;
            publishTouchState(resultJson);
            LOGI("verify %s count=%d auto=%d -> %s\n", btnName.c_str(), count, autoHandle, resultJson.c_str());
            return;
        }

        // Kompensation: Der kapazitive Controller der WP "verschluckt" beim
        // Mehrfachdruck zuverlässig den ersten Tastendruck (er dient nur zum
        // "Aufwecken"/Aktivieren der Eingabe). Bei count>=2 daher einen Druck
        // zusätzlich senden, damit die tatsächliche Wertänderung dem count
        // entspricht (z.B. count=5 -> echte +5). Beim Einzeldruck (count=1)
        // NICHT kompensieren: dort ist das Verschlucken neutral/gewollt.
        // Hinweis: Eine robustere Lösung (OCR-Closed-Loop) ist als Erweiterung
        // geplant; dies ist die pragmatische Zwischenlösung.
        uint8_t effectiveCount = (count >= 2) ? (count + 1) : count;

        r = WpTouch.press(btnName, effectiveCount);
    }

    if (g_cfgRef && g_mqttClient && g_mqttClient->connected()) {
        String st = g_cfgRef->mqtt.topic + WP_MQTT_TOPIC_STATE_SUFFIX;
        String pl = "{\"last\":\"" + btnName + "\",\"ok\":" + (r ? "true" : "false") + "}";
        g_mqttClient->publish(st.c_str(), pl.c_str(), false);
    }
}

void mqttRegisterTouchConfig(AppConfig* cfg) {
    g_cfgRef = cfg;
}

void mqttSetGlobalClient(PubSubClient *mqtt) {
    g_mqttClient = mqtt;
}

// ─── MQTT Helpers ─────────────────────────────────────────────────────────────
static String mqttAvailabilityTopic(const AppConfig &cfg) {
    return cfg.mqtt.topic + "/availability";
}

static String mqttImageTopic(const AppConfig &cfg) {
    return cfg.mqtt.topic + "/snapshot";
}

// ─── Connect / Reconnect ─────────────────────────────────────────────────────
bool mqttEnsureConnected(PubSubClient &mqtt, const AppConfig &cfg) {
    if (!cfg.mqtt.enabled) {
        LOGD("MQTT disabled\n");
        return false;
    }

    if (mqtt.connected()) return true;

    String cid       = "hp-remote-" + devicePersistentUid();
    String availTopic = mqttAvailabilityTopic(cfg);

    LOGI("Connecting MQTT to %s:%u\n", cfg.mqtt.host.c_str(), cfg.mqtt.port);

    bool ok = false;
    if (cfg.mqtt.username.length() > 0) {
        ok = mqtt.connect(cid.c_str(),
                          cfg.mqtt.username.c_str(),
                          cfg.mqtt.password.c_str(),
                          availTopic.c_str(), 0, true, "offline");
    } else {
        ok = mqtt.connect(cid.c_str(),
                          availTopic.c_str(), 0, true, "offline");
    }

    if (!ok) {
        LOGE("MQTT connect failed, rc=%d\n", mqtt.state());
        return false;
    }

    LOGI("MQTT connected\n");
    mqtt.publish(availTopic.c_str(), "online", true);

    // Touch-Command Topic abonnieren
    if (g_cfgRef) {
        String cmdTopic = g_cfgRef->mqtt.topic + WP_MQTT_TOPIC_CMD_SUFFIX;
        mqtt.subscribe(cmdTopic.c_str());
        LOGI("MQTT subscribed: %s\n", cmdTopic.c_str());
    }

    mqttPublishHomeAssistantDiscovery(mqtt, cfg);
    mqttPublishTouchDiscovery(mqtt, cfg);
    mqttPublishStreamDiscovery(mqtt, cfg);

    // Heizraum-Sensor: Discovery nur, wenn er aktuell erkannt ist – sonst
    // bleibt er für Home Assistant unsichtbar (siehe room_sensor.h).
    if (RoomSensorSht31.isAvailable()) {
        mqttPublishRoomSensorDiscovery(mqtt, cfg);
    }

    return true;
}

void mqttApplyConfig(PubSubClient &mqtt, const AppConfig &cfg) {
    mqtt.setServer(cfg.mqtt.host.c_str(), cfg.mqtt.port);
}

void mqttPublishState(PubSubClient &mqtt, const AppConfig &cfg, const String &payload) {
    if (!mqttEnsureConnected(mqtt, cfg)) return;
    bool ok = mqtt.publish(cfg.mqtt.topic.c_str(), payload.c_str(), true);
    if (!ok) LOGE("MQTT publish failed: %s\n", cfg.mqtt.topic.c_str());
}

void mqttPublishHealth(PubSubClient &mqtt, const AppConfig &cfg, const String &payload) {
    if (!mqttEnsureConnected(mqtt, cfg)) return;
    String topic = cfg.mqtt.topic + "/health";
    bool ok = mqtt.publish(topic.c_str(), payload.c_str(), true);
    if (!ok) LOGE("MQTT health publish failed\n");
}

void mqttPublishImage(PubSubClient &mqtt, const AppConfig &cfg,
                      const uint8_t *payload, size_t len) {
    if (!cfg.mqtt.image_enabled) return;
    if (!payload || len == 0) return;
    if (!mqttEnsureConnected(mqtt, cfg)) return;
    String topic = mqttImageTopic(cfg);
    bool ok = mqtt.publish(topic.c_str(), payload, (unsigned int)len, true);
    if (!ok) LOGE("MQTT image publish failed\n");
}

// ─── Heizraum-Sensor (SHT31, optional) ───────────────────────────────────────
static String mqttRoomStateTopic(const AppConfig &cfg) {
    return cfg.mqtt.topic + "/room/state";
}

static String mqttRoomAvailabilityTopic(const AppConfig &cfg) {
    return cfg.mqtt.topic + "/room/availability";
}

void mqttPublishRoomSensorState(PubSubClient &mqtt, const AppConfig &cfg, const String &payload) {
    if (!mqttEnsureConnected(mqtt, cfg)) return;
    mqtt.publish(mqttRoomAvailabilityTopic(cfg).c_str(), "online", true);
    bool ok = mqtt.publish(mqttRoomStateTopic(cfg).c_str(), payload.c_str(), true);
    if (!ok) LOGE("MQTT room sensor publish failed\n");
}

void mqttSetRoomSensorAvailability(PubSubClient &mqtt, const AppConfig &cfg, bool available) {
    if (!mqttEnsureConnected(mqtt, cfg)) return;
    mqtt.publish(mqttRoomAvailabilityTopic(cfg).c_str(), available ? "online" : "offline", true);
}