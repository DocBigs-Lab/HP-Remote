#include "mqtt_ha_discovery.h"
#include "build_info.h"
#include <WiFi.h>

#include <ArduinoJson.h>
#include <ctype.h>

#include "debug_log.h"
#include "device_identity.h"

static String sanitizeId(const String &in) {
  String out;
  out.reserve(in.length());

  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (isalnum((unsigned char)c)) {
      out += (char)tolower((unsigned char)c);
    } else {
      out += '_';
    }
  }

  while (out.indexOf("__") >= 0) {
    out.replace("__", "_");
  }

  return out;
}

static String toLowerCopy(const String &in) {
  String out = in;
  out.toLowerCase();
  return out;
}

static bool containsAny(const String &text, const char *const patterns[], size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (text.indexOf(patterns[i]) >= 0) {
      return true;
    }
  }
  return false;
}

static String roiSearchText(const Roi &roi) {
  return toLowerCopy(roi.id + " " + roi.label);
}

static bool isErrorLikeRoi(const Roi &roi) {
  String s = roiSearchText(roi);
  static const char *errorKeys[] = {
    "error", "err", "fault", "alarm", "warn", "warning", "fehler", "stoerung"
  };
  return containsAny(s, errorKeys, sizeof(errorKeys) / sizeof(errorKeys[0]));
}

static String haComponentForRoi(const Roi &roi) {
  if (roi.type == "symbol") {
    return "binary_sensor";
  }
  return "sensor";
}

static String haObjectIdForRoi(const AppConfig &cfg, const Roi &roi) {
  return sanitizeId("hp_" + deviceMacSuffix() + "_" + roi.id);
}

static String haConfigTopicForRoi(const AppConfig &cfg, const Roi &roi) {
  String component = haComponentForRoi(roi);
  String objectId = haObjectIdForRoi(cfg, roi);
  return cfg.ha.discovery_prefix + "/" + component + "/" + objectId + "/config";
}

static String mqttAvailabilityTopic(const AppConfig &cfg) {
  return cfg.mqtt.topic + "/availability";
}

static String mqttImageTopic(const AppConfig &cfg) {
  return cfg.mqtt.topic + "/snapshot";
}

static String haCameraObjectId(const AppConfig &cfg) {
  return sanitizeId("hp_" + deviceMacSuffix() + "_snapshot");
}

static String haCameraConfigTopic(const AppConfig &cfg) {
  return cfg.ha.discovery_prefix + "/image/" + haCameraObjectId(cfg) + "/config";
}

static String haEntityNameForRoi(const Roi &roi) {
  if (roi.ha_name.length() > 0) {
    return roi.ha_name;
  }
  if (roi.label.length() > 0) {
    return roi.label;
  }
  return roi.id;
}

static String haValueTemplateForRoi(const Roi &roi) {
  if (roi.type == "symbol") {
    return "{{ 'ON' if value_json['" + roi.id + "'] else 'OFF' }}";
  }
  return "{{ value_json['" + roi.id + "'] }}";
}

static String autoUnitForRoi(const Roi &roi) {
  if (roi.ha_unit.length() > 0) {
    return roi.ha_unit;
  }

  if (isErrorLikeRoi(roi)) {
    if (roi.type == "symbol") {
      return "";
    }
    return "code";
  }

  String s = roiSearchText(roi);

  static const char *tempKeys[] = {"temp", "temperature", "celsius", "flow temp", "water temp"};
  static const char *humidKeys[] = {"humidity", "humid", "rh"};
  static const char *pressureKeys[] = {"pressure", "press", "bar", "mbar"};
  static const char *powerKeys[] = {"power", "watt", "kw", "kilowatt"};
  static const char *voltageKeys[] = {"voltage", "volt"};
  static const char *currentKeys[] = {"current", "amp", "amps", "ampere"};
  
  static const char *energyKeys[] = {"energy", "kwh", "wh"};
  static const char *timeKeys[] = {"runtime", "run time", "hours", "hour", "time"};
  static const char *volumeKeys[] = {"liter", "litre", "volume", "tank", "fill", "level"};

  if (containsAny(s, tempKeys, sizeof(tempKeys) / sizeof(tempKeys[0]))) return "°C";
  if (containsAny(s, humidKeys, sizeof(humidKeys) / sizeof(humidKeys[0]))) return "%";
  if (containsAny(s, pressureKeys, sizeof(pressureKeys) / sizeof(pressureKeys[0]))) return "bar";
  if (containsAny(s, powerKeys, sizeof(powerKeys) / sizeof(powerKeys[0]))) return "W";
  if (containsAny(s, voltageKeys, sizeof(voltageKeys) / sizeof(voltageKeys[0]))) return "V";
  if (containsAny(s, currentKeys, sizeof(currentKeys) / sizeof(currentKeys[0]))) return "A";
  if (containsAny(s, energyKeys, sizeof(energyKeys) / sizeof(energyKeys[0]))) return "kWh";
  if (containsAny(s, timeKeys, sizeof(timeKeys) / sizeof(timeKeys[0]))) return "h";
  if (containsAny(s, volumeKeys, sizeof(volumeKeys) / sizeof(volumeKeys[0]))) return "l";

  return "";
}

static String autoIconForRoi(const Roi &roi) {
  if (roi.ha_icon.length() > 0) {
    return roi.ha_icon;
  }

  if (isErrorLikeRoi(roi)) return "mdi:alert";

  String s = roiSearchText(roi);

  static const char *tempKeys[] = {"temp", "temperature", "celsius", "flow temp", "water temp"};
  static const char *humidKeys[] = {"humidity", "humid", "rh"};
  static const char *pressureKeys[] = {"pressure", "press", "bar", "mbar"};
  static const char *powerKeys[] = {"power", "watt", "kw", "kilowatt"};
  static const char *voltageKeys[] = {"voltage", "volt"};
  static const char *currentKeys[] = {"current", "amp", "amps", "ampere"};
  static const char *energyKeys[] = {"energy", "kwh", "wh"};
  static const char *timeKeys[] = {"runtime", "run time", "hours", "hour", "time"};
  static const char *volumeKeys[] = {"liter", "litre", "volume", "tank", "fill", "level"};
  static const char *heatKeys[] = {"heat", "heating", "burner"};
  static const char *flowKeys[] = {"flow"};
  if (containsAny(s, tempKeys, sizeof(tempKeys) / sizeof(tempKeys[0]))) return "mdi:thermometer";
  if (containsAny(s, humidKeys, sizeof(humidKeys) / sizeof(humidKeys[0]))) return "mdi:water-percent";
  if (containsAny(s, pressureKeys, sizeof(pressureKeys) / sizeof(pressureKeys[0]))) return "mdi:gauge";
  if (containsAny(s, powerKeys, sizeof(powerKeys) / sizeof(powerKeys[0]))) return "mdi:flash";
  if (containsAny(s, voltageKeys, sizeof(voltageKeys) / sizeof(voltageKeys[0]))) return "mdi:sine-wave";
  if (containsAny(s, currentKeys, sizeof(currentKeys) / sizeof(currentKeys[0]))) return "mdi:current-ac";
  if (containsAny(s, energyKeys, sizeof(energyKeys) / sizeof(energyKeys[0]))) return "mdi:lightning-bolt";
  if (containsAny(s, timeKeys, sizeof(timeKeys) / sizeof(timeKeys[0]))) return "mdi:timer-outline";
  if (containsAny(s, volumeKeys, sizeof(volumeKeys) / sizeof(volumeKeys[0]))) return "mdi:cup-water";
  if (containsAny(s, heatKeys, sizeof(heatKeys) / sizeof(heatKeys[0]))) return "mdi:radiator";
  if (containsAny(s, flowKeys, sizeof(flowKeys) / sizeof(flowKeys[0]))) return "mdi:waves-arrow-right";

  if (roi.type == "symbol") {
    return "mdi:toggle-switch";
  }

  return "mdi:counter";
}

static String autoDeviceClassForRoi(const Roi &roi) {
  if (roi.ha_device_class.length() > 0) {
    return roi.ha_device_class;
  }

  if (isErrorLikeRoi(roi)) {
    if (roi.type == "symbol") return "problem";
    return "";
  }

  String s = roiSearchText(roi);

  static const char *tempKeys[] = {"temp", "temperature", "celsius"};
  static const char *humidKeys[] = {"humidity", "humid", "rh"};
  static const char *pressureKeys[] = {"pressure", "press", "bar", "mbar"};
  static const char *powerKeys[] = {"power", "watt", "kw", "kilowatt"};
  static const char *voltageKeys[] = {"voltage", "volt"};
  static const char *currentKeys[] = {"current", "amp", "amps", "ampere"};
  static const char *energyKeys[] = {"energy", "kwh", "wh"};
  static const char *durationKeys[] = {"runtime", "run time", "hours", "hour", "time"};
  static const char *heatKeys[] = {"heat", "heating", "burner"};
  if (roi.type == "symbol") {
    if (containsAny(s, heatKeys, sizeof(heatKeys) / sizeof(heatKeys[0]))) return "heat";
    return "";
  }

  if (containsAny(s, tempKeys, sizeof(tempKeys) / sizeof(tempKeys[0]))) return "temperature";
  if (containsAny(s, humidKeys, sizeof(humidKeys) / sizeof(humidKeys[0]))) return "humidity";
  if (containsAny(s, pressureKeys, sizeof(pressureKeys) / sizeof(pressureKeys[0]))) return "pressure";
  if (containsAny(s, powerKeys, sizeof(powerKeys) / sizeof(powerKeys[0]))) return "power";
  if (containsAny(s, voltageKeys, sizeof(voltageKeys) / sizeof(voltageKeys[0]))) return "voltage";
  if (containsAny(s, currentKeys, sizeof(currentKeys) / sizeof(currentKeys[0]))) return "current";
  if (containsAny(s, energyKeys, sizeof(energyKeys) / sizeof(energyKeys[0]))) return "energy";
  if (containsAny(s, durationKeys, sizeof(durationKeys) / sizeof(durationKeys[0]))) return "duration";

  return "";
}

static String autoStateClassForRoi(const Roi &roi) {
  if (roi.ha_state_class.length() > 0) {
    return roi.ha_state_class;
  }

  if (isErrorLikeRoi(roi)) {
    return "";
  }

  if (roi.type == "symbol") {
    return "";
  }

  String s = roiSearchText(roi);

  static const char *energyKeys[] = {"energy", "kwh", "wh"};
  if (containsAny(s, energyKeys, sizeof(energyKeys) / sizeof(energyKeys[0]))) {
    return "total_increasing";
  }

  return "measurement";
}


static bool publishOnce(PubSubClient &mqtt, const String &topic, const String &payload) {
  bool ok = mqtt.publish(topic.c_str(), payload.c_str(), true);
  if (!ok) {
    LOGE("MQTT publish failed (state=%d len=%u), retrying in 500ms: topic=%s\n",
         mqtt.state(), (unsigned)payload.length(), topic.c_str());
    delay(500);  // longer pause so lwIP can fully drain TCP write buffer
    ok = mqtt.publish(topic.c_str(), payload.c_str(), true);
    if (!ok) {
      LOGE("MQTT publish retry failed (state=%d): topic=%s\n",
           mqtt.state(), topic.c_str());
    }
  }
  return ok;
}

static bool publishDiscoveryForRoi(PubSubClient &mqtt, const AppConfig &cfg, const Roi &roi) {
  if (!roi.ha_enabled) {
    LOGI("HA discovery skipped for roi=%s (ha_enabled=false)\n", roi.id.c_str());
    return true;
  }

  String component = haComponentForRoi(roi);
  String topic = haConfigTopicForRoi(cfg, roi);
  String objectId = haObjectIdForRoi(cfg, roi);

  JsonDocument doc;

  doc["name"] = haEntityNameForRoi(roi);
  doc["object_id"] = objectId;
  doc["unique_id"] = devicePersistentUid() + "_" + sanitizeId(roi.id);
  doc["state_topic"] = cfg.mqtt.topic;
  doc["value_template"] = haValueTemplateForRoi(roi);
  doc["availability_topic"] = mqttAvailabilityTopic(cfg);
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";

  String unit = autoUnitForRoi(roi);
  String icon = autoIconForRoi(roi);
  String deviceClass = autoDeviceClassForRoi(roi);
  String stateClass = autoStateClassForRoi(roi);

  if (unit.length() > 0 && component == "sensor") {
    doc["unit_of_measurement"] = unit;
  }

  if (icon.length() > 0) {
    doc["icon"] = icon;
  }

  if (deviceClass.length() > 0) {
    doc["device_class"] = deviceClass;
  }

  if (stateClass.length() > 0 && component == "sensor" && unit.length() > 0) {
    doc["state_class"] = stateClass;
  }

  if (component == "binary_sensor") {
    doc["payload_on"] = "ON";
    doc["payload_off"] = "OFF";
  }

  JsonObject device = doc["device"].to<JsonObject>();
  JsonArray identifiers = device["identifiers"].to<JsonArray>();
  identifiers.add(devicePersistentUid());
  device["name"] = cfg.device.name;
  device["manufacturer"] = "Dr.Big";
  device["model"] = "HP-Remote";

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
  device["sw_version"] = FW_VERSION;

  String payload;
  serializeJson(doc, payload);

  LOGI("HA discovery publish: roi=%s component=%s payload_len=%u topic=%s\n",
       roi.id.c_str(),
       component.c_str(),
       (unsigned)payload.length(),
       topic.c_str());

  bool ok = publishOnce(mqtt, topic, payload);

  if (ok) {
    LOGI("HA discovery publish ok: roi=%s\n", roi.id.c_str());
  }

  return ok;
}

static void publishDiscoveryForSnapshot(PubSubClient &mqtt, const AppConfig &cfg) {
  String topic = haCameraConfigTopic(cfg);  // now points to /image/

  if (!cfg.mqtt.image_enabled) {
    // Clear both old camera and new image entries
    String oldCameraTopic = cfg.ha.discovery_prefix + "/camera/" + haCameraObjectId(cfg) + "/config";
    mqtt.publish(oldCameraTopic.c_str(), "", true);
    mqtt.publish(topic.c_str(), "", true);
    LOGI("HA image discovery removed (image publishing disabled)\n");
    return;
  }

  JsonDocument doc;
  doc["name"] = "Snapshot";
  doc["object_id"] = haCameraObjectId(cfg);
  doc["unique_id"] = devicePersistentUid() + "_snapshot";
  // HA MQTT image entity (homeassistant/image/) uses "image_topic" for raw binary JPEG
  doc["image_topic"] = mqttImageTopic(cfg);
  doc["availability_topic"] = mqttAvailabilityTopic(cfg);
  doc["payload_available"] = "online";
  doc["payload_not_available"] = "offline";
  doc["icon"] = "mdi:image";

  JsonObject device = doc["device"].to<JsonObject>();
  JsonArray identifiers = device["identifiers"].to<JsonArray>();
  identifiers.add(devicePersistentUid());
  device["name"] = cfg.device.name;
  device["manufacturer"] = "Dr.Big";
  device["model"] = "HP-Remote";

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
  device["sw_version"] = FW_VERSION;

  String payload;
  serializeJson(doc, payload);

  bool ok = publishOnce(mqtt, topic, payload);
  if (ok) {
    LOGI("HA camera discovery publish ok: topic=%s\n", topic.c_str());
  }
}

void mqttPublishHomeAssistantDiscovery(PubSubClient &mqtt, const AppConfig &cfg) {
  if (!cfg.ha.enabled) {
    LOGI("HA discovery disabled in config\n");
    return;
  }
  if (!cfg.mqtt.enabled) {
    LOGD("HA discovery skipped: MQTT disabled\n");
    return;
  }

  if (!mqtt.connected()) {
    LOGD("HA discovery skipped: MQTT not connected\n");
    return;
  }

  LOGI("Publishing Home Assistant discovery for %u ROIs\n",
       (unsigned)cfg.rois.size());

  for (const auto &roi : cfg.rois) {
    publishDiscoveryForRoi(mqtt, cfg, roi);
    delay(50);  // give WiFi/lwIP time to drain TCP write buffer (AP+STA mode needs more)
  }

  publishDiscoveryForSnapshot(mqtt, cfg);
  delay(50);

}

// ─── HP-Remote: Touch-Button Discovery ───────────────────────────────────────
// Wird automatisch nach dem bestehenden mqttPublishHomeAssistantDiscovery()
// aufgerufen – fügt 6 button-Entities in HA ein.
void mqttPublishTouchDiscovery(PubSubClient &mqtt, const AppConfig &cfg) {
    if (!cfg.ha.enabled) return;
    if (!cfg.ha.discovery_prefix.length()) return;

    struct BtnDef { const char* id; const char* name; const char* icon; const char* payload; };
    static const BtnDef btns[] = {
        { "power",    "HP Power",    "mdi:power",          "{\"button\":\"power\"}" },
        { "boost",    "HP Boost",    "mdi:lightning-bolt", "{\"button\":\"boost\"}" },
        { "settings", "HP Settings", "mdi:cog",            "{\"button\":\"settings\"}" },
        { "time",     "HP Time",    "mdi:clock-outline",  "{\"button\":\"time\"}" },
        { "up",       "HP ↑",    "mdi:chevron-up",     "{\"button\":\"up\"}" },
        { "down",     "HP ↓",    "mdi:chevron-down",   "{\"button\":\"down\"}" },
    };

    String cmdTopic   = cfg.mqtt.topic + "/touch/cmd";
    String stateTopic = cfg.mqtt.topic + "/touch/state";
    String availTopic = cfg.mqtt.topic + "/availability";

    for (const auto &b : btns) {
        String uid = devicePersistentUid() + "_touch_" + b.id;
        String topic = cfg.ha.discovery_prefix + "/button/" + uid + "/config";

        JsonDocument doc;
        doc["name"]             = b.name;
        doc["unique_id"]        = uid;
        doc["object_id"]        = "hp_" + deviceMacSuffix() + "_" + b.id;
        doc["command_topic"]    = cmdTopic;
        doc["payload_press"]    = b.payload;
        doc["availability_topic"] = availTopic;
        doc["payload_available"]   = "online";
        doc["payload_not_available"] = "offline";
        doc["icon"]             = b.icon;
        doc["retain"]           = false;

        JsonObject device = doc["device"].to<JsonObject>();
        JsonArray ids = device["identifiers"].to<JsonArray>();
        ids.add(devicePersistentUid());
        device["name"]         = cfg.device.name;
        device["manufacturer"] = "Dr.Big";
        device["model"]        = "HP-Remote";
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
        device["sw_version"]   = FW_VERSION;

        String payload;
        serializeJson(doc, payload);
        bool ok = mqtt.publish(topic.c_str(), payload.c_str(), true);
        LOGI("HA touch button discovery %s: %s (%u bytes)\n",
             b.id, ok ? "ok" : "fail", (unsigned)payload.length());
        mqtt.loop();      // Client bedienen damit Paket rausgeht
        delay(30);
    }
}

// ─── HP-Remote: Heizraum-Sensor Discovery (SHT31, optional) ──────────────────
// Eigene Availability-Topic (nicht die geräteweite) – damit HA die beiden
// Entities als "unavailable" markiert, sobald der Sensor selbst verschwindet,
// auch wenn das Gerät per MQTT weiterhin online ist.
static String mqttRoomStateTopic(const AppConfig &cfg) {
  return cfg.mqtt.topic + "/room/state";
}

static String mqttRoomAvailabilityTopic(const AppConfig &cfg) {
  return cfg.mqtt.topic + "/room/availability";
}

void mqttPublishRoomSensorDiscovery(PubSubClient &mqtt, const AppConfig &cfg) {
  if (!cfg.ha.enabled) return;
  if (!cfg.ha.discovery_prefix.length()) return;

  struct RoomSensorDef { const char* key; const char* name; const char* unit; const char* deviceClass; const char* icon; };
  static const RoomSensorDef defs[] = {
    { "room_temp_c",      "HP Heizraum Temperatur",  "°C", "temperature", "mdi:thermometer" },
    { "room_humidity_pct", "HP Heizraum Luftfeuchte", "%",  "humidity",    "mdi:water-percent" },
  };

  String stateTopic = mqttRoomStateTopic(cfg);
  String availTopic = mqttRoomAvailabilityTopic(cfg);

  for (const auto &d : defs) {
    String objectId = sanitizeId("hp_" + deviceMacSuffix() + "_" + d.key);
    String uid = devicePersistentUid() + "_" + d.key;
    String topic = cfg.ha.discovery_prefix + "/sensor/" + objectId + "/config";

    JsonDocument doc;
    doc["name"] = d.name;
    doc["object_id"] = objectId;
    doc["unique_id"] = uid;
    doc["state_topic"] = stateTopic;
    doc["value_template"] = String("{{ value_json['") + d.key + "'] }}";
    doc["availability_topic"] = availTopic;
    doc["payload_available"] = "online";
    doc["payload_not_available"] = "offline";
    doc["unit_of_measurement"] = d.unit;
    doc["device_class"] = d.deviceClass;
    doc["state_class"] = "measurement";
    doc["icon"] = d.icon;

    JsonObject device = doc["device"].to<JsonObject>();
    JsonArray identifiers = device["identifiers"].to<JsonArray>();
    identifiers.add(devicePersistentUid());
    device["name"] = cfg.device.name;
    device["manufacturer"] = "Dr.Big";
    device["model"] = "HP-Remote";

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
    device["sw_version"] = FW_VERSION;

    String payload;
    serializeJson(doc, payload);

    bool ok = publishOnce(mqtt, topic, payload);
    LOGI("HA room sensor discovery %s: %s (%u bytes)\n", d.key, ok ? "ok" : "fail", (unsigned)payload.length());
    mqtt.loop();
    delay(30);
  }
}

// ─── HP-Remote: Live-Page Discovery ──────────────────────────────────────────
// Publisht einen HA button der http://[IP]/live öffnet (Stream + Touch-Buttons)
void mqttPublishStreamDiscovery(PubSubClient &mqtt, const AppConfig &cfg) {
    if (!cfg.ha.enabled) return;
    if (!cfg.ha.discovery_prefix.length()) return;

    String ip = (WiFi.status() == WL_CONNECTED)
                  ? WiFi.localIP().toString()
                  : WiFi.softAPIP().toString();

    String liveUrl = "http://" + ip + "/live";
    String uid     = devicePersistentUid() + "_live";

    // Live-URL als retained Sensor publishen – für HA Automationen / Dashboard
    {
        String uid2  = devicePersistentUid() + "_live_url";
        String topic = cfg.ha.discovery_prefix + "/sensor/" + uid2 + "/config";
        JsonDocument doc;
        doc["name"]            = "HP-Remote Live URL";
        doc["unique_id"]       = uid2;
        doc["object_id"]       = "hp_" + deviceMacSuffix() + "_live_url";
        doc["state_topic"]     = cfg.mqtt.topic + "/live_url";
        doc["icon"]            = "mdi:web";
        doc["entity_category"] = "diagnostic";

        JsonObject device = doc["device"].to<JsonObject>();
        device["identifiers"][0] = devicePersistentUid();
        device["name"]           = cfg.device.name;
        device["manufacturer"]   = "Dr.Big";
        device["model"]          = "HP-Remote";

        String payload;
        serializeJson(doc, payload);
        mqtt.publish(topic.c_str(), payload.c_str(), true);
        mqtt.loop();
        delay(30);

        // URL publishen
        mqtt.publish((cfg.mqtt.topic + "/live_url").c_str(),
                     liveUrl.c_str(), true);
        mqtt.loop();
        delay(30);
        LOGI("Live URL published: %s\n", liveUrl.c_str());
    }
}
