#include "storage_service.h"
#include <Preferences.h>

static Preferences prefs;
static const char* NS = "wpr";     // Namespace
static const char* KEY = "cfg";    // Key für Config-JSON

bool storageBegin() {
  bool ok = prefs.begin(NS, false);
  Serial.printf("[STG] Preferences.begin: %s\n", ok ? "OK" : "FAIL");
  return ok;
}

bool loadConfigFromFile(AppConfig &cfg) {
  if (!prefs.isKey(KEY)) {
    Serial.println("[STG] No config in NVS – using defaults");
    return false;
  }

  // Als Blob lesen (unterstützt größere Configs als getString)
  size_t len = prefs.getBytesLength(KEY);

  // Migration: alter putString-Key? Dann getBytesLength == 0 obwohl Key existiert
  if (len == 0) {
    String legacy = prefs.getString(KEY, "");
    if (legacy.length() >= 5) {
      Serial.printf("[STG] Migrating legacy String config (%u bytes) -> Blob\n",
                    (unsigned)legacy.length());
      bool ok = configFromJson(legacy, cfg);
      // Sofort als Blob neu schreiben
      prefs.remove(KEY);
      prefs.putBytes(KEY, legacy.c_str(), legacy.length());
      return ok;
    }
    Serial.println("[STG] Config in NVS empty/too small");
    return false;
  }

  String json;
  json.reserve(len + 1);
  {
    char *buf = (char*)malloc(len + 1);
    if (!buf) {
      Serial.println("[STG] malloc failed for config load");
      return false;
    }
    size_t got = prefs.getBytes(KEY, buf, len);
    buf[got] = '\0';
    json = String(buf);
    free(buf);
  }

  bool ok = configFromJson(json, cfg);
  Serial.printf("[STG] Config load: %s (%u bytes)\n",
                ok ? "OK" : "FAIL", (unsigned)json.length());
  // DEBUG: locked-Status der geladenen ROIs
  for (const auto &r : cfg.rois) {
    Serial.printf("[STG] LOAD roi=%s locked=%d\n", r.id.c_str(), r.locked ? 1 : 0);
  }
  return ok;
}

bool saveConfigToFile(const AppConfig &cfg) {
  String json = configToJson(cfg);
  // DEBUG: locked-Status der ROIs loggen
  for (const auto &r : cfg.rois) {
    Serial.printf("[STG] SAVE roi=%s locked=%d\n", r.id.c_str(), r.locked ? 1 : 0);
  }
  // Als Blob speichern (putBytes) – unterstützt deutlich größere Configs als putString
  size_t written = prefs.putBytes(KEY, json.c_str(), json.length());
  Serial.printf("[STG] Config save: %u of %u bytes written\n",
                (unsigned)written, (unsigned)json.length());
  if (written != json.length()) {
    Serial.printf("[STG] *** WARNING: NVS write incomplete! ***\n");
  }
  return written == json.length();
}