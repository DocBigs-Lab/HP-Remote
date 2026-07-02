#include <Arduino.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include <math.h>
#include "esp_heap_caps.h"

#include "app_config.h"
#include "device_identity.h"
#include "debug_log.h"
#include "storage_service.h"
#include "wifi_manager.h"
#include "camera_service.h"
#include "analyzer.h"
#include "mqtt_service.h"
#include "mqtt_ha_discovery.h"
#include "web_server.h"
#include "ota_service.h"
#include "touch_controller.h"
#include "touch_config.h"
#include "room_sensor.h"
#include "build_info.h"   // generiert von version_build.py (FW_VERSION, FW_BUILD)

#include "esp_ota_ops.h"
#include "esp_partition.h"

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
WebServer server(80);

AppConfig cfg;

static String evaluateMedian3State(AppConfig &cfg);

String lastPayload = "{}";
String lastGoodPayload = "{}";

unsigned long lastEvalMs = 0;
unsigned long lastHealthMs = 0;
unsigned long lastRoomSensorMs = 0;
bool roomSensorWasAvailable = false;

WifiModeState wifiState = WIFI_MODE_AP_SETUP;

static const int CAMERA_RESTART_AFTER_FAILS = 3;
static const unsigned long HEALTH_INTERVAL_MS = 60000UL;
static const uint16_t MQTT_BUFFER_SIZE_DEFAULT = 2048;
static const uint16_t MQTT_BUFFER_SIZE_WITH_IMAGE = 32768;

static bool majority3Bool(bool a, bool b, bool c) {
  return (a + b + c) >= 2;
}

static double median3Double(double a, double b, double c) {
  if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
  if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
  return c;
}

String runActiveEvaluation(AppConfig &cfg) {
  return evaluateMedian3State(cfg);
}

static String buildHealthPayload() {
  JsonDocument doc;

  doc["uptime_s"] = millis() / 1000UL;

  doc["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
  doc["wifi_rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
  doc["ip"] = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : "";

  doc["mqtt_connected"] = mqtt.connected();

  doc["camera_ok"] = cameraIsHealthy();
  doc["camera_model"] = "ESP32-S3-CAM";
  doc["camera_sensor"] = cameraSensorName();
  doc["camera_frame"] = String(cfg.camera.width) + "x" + String(cfg.camera.height);

  doc["camera_capture_failures"] = cameraCaptureFailureCount();

  doc["heap_free"] = ESP.getFreeHeap();

  String out;
  serializeJson(doc, out);
  return out;
}

// Rotiert einen GRAYSCALE-Framebuffer in-place um den Bildmittelpunkt.
// Kleinwinklig (±5°), Rückwärts-Sampling (Nearest-Neighbor) in temporären Puffer.
static void rotateGrayscaleFb(camera_fb_t *fb, float deg) {
  if (!fb || fb->format != PIXFORMAT_GRAYSCALE) return;
  if (deg > -0.05f && deg < 0.05f) return;   // unter 0.05° nicht lohnenswert

  const int W = fb->width;
  const int H = fb->height;
  uint8_t *src = (uint8_t*)heap_caps_malloc(W * H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!src) src = (uint8_t*)malloc(W * H);
  if (!src) return;
  memcpy(src, fb->buf, W * H);

  const float rad = deg * 3.14159265f / 180.0f;
  const float cs = cosf(rad);
  const float sn = sinf(rad);
  const float cx = W / 2.0f;
  const float cy = H / 2.0f;

  for (int y = 0; y < H; y++) {
    const float dy = y - cy;
    for (int x = 0; x < W; x++) {
      const float dx = x - cx;
      // Rückwärts: Quellposition für Zielpixel (x,y)
      int sx = (int)(cx + dx * cs + dy * sn + 0.5f);
      int sy = (int)(cy - dx * sn + dy * cs + 0.5f);
      uint8_t val = 128;   // Rand = neutralgrau
      if (sx >= 0 && sx < W && sy >= 0 && sy < H)
        val = src[sy * W + sx];
      fb->buf[y * W + x] = val;
    }
  }
  free(src);
}

static bool acquireStateFrame(JsonDocument &doc) {
  camera_fb_t *fb = cameraCapture();
  if (!fb) {
    LOGE("cameraCapture() failed\n");
    cameraMarkCaptureFailure();
    return false;
  }
  rotateGrayscaleFb(fb, cfg.camera.fine_rotation);
  bool ok = evaluateStateToJson(fb, cfg, doc);
  cameraRelease(fb);
  cameraMarkCaptureSuccess();
  return ok;
}

// ───────────────────────── Closed-Loop Solltemperatur ──────────────────────
// Sucht ein ROI dessen LABEL (case-insensitive) einen der übergebenen
// Begriffe enthält und liefert dessen roi.id zurück (Schlüssel im State-JSON).
// Leerer Rückgabestring = nicht gefunden.
static String findRoiIdByLabel(const char* const* needles, int n) {
  for (auto &roi : cfg.rois) {
    String lbl = roi.label;
    lbl.toLowerCase();
    for (int i = 0; i < n; i++) {
      if (lbl.indexOf(needles[i]) >= 0) return roi.id;
    }
  }
  return "";
}

// Liest den aktuellen Wert eines sevenseg-ROI (per id) aus einem frischen Frame.
// Rückgabe: Wert als int, oder -1 wenn nicht lesbar / nicht vorhanden.
static int readRoiIntValue(const String &roiId) {
  if (roiId.length() == 0) return -1;
  JsonDocument doc;
  if (!acquireStateFrame(doc)) return -1;
  if (!doc[roiId].is<int>()) {
    // Bei Dezimalstellen liefert das ROI einen String – hier auf int runden.
    if (doc[roiId].is<const char*>()) {
      return (int)round(atof(doc[roiId].as<const char*>()));
    }
    return -1;
  }
  return doc[roiId].as<int>();
}

// Stabile Lesung: liest bis zu 3x und gibt erst zurück, wenn zwei aufeinander-
// folgende Lesungen übereinstimmen (entprellt einzelne Fehllesungen, z.B.
// während sich das Display noch aufbaut). Fällt sonst auf die letzte Lesung
// zurück. Kostet im Worst Case 3 Frames, im Normalfall 2.
static int readRoiStable(const String &roiId) {
  int a = readRoiIntValue(roiId);
  delay(120);
  yield();
  int b = readRoiIntValue(roiId);
  if (a == b) return a;          // zwei gleiche → stabil
  // dritte Lesung als Tie-Breaker
  delay(120);
  yield();
  int c = readRoiIntValue(roiId);
  if (c == a || c == b) return c;
  return c;                      // alle verschieden → neueste nehmen
}

// Prüft ob die Tastensperre aktiv ist (Locked-Symbol an).
// Rückgabe: true = gesperrt. Wenn kein Locked-ROI existiert: false (unbekannt).
static bool isDisplayLocked() {
  static const char* lockNeedles[] = { "lock" };
  String lockId = findRoiIdByLabel(lockNeedles, 1);
  if (lockId.length() == 0) return false;   // kein Locked-ROI → nicht prüfbar
  JsonDocument doc;
  if (!acquireStateFrame(doc)) return false;
  return doc[lockId].is<bool>() && doc[lockId].as<bool>();
}

// Ob überhaupt ein Locked-ROI konfiguriert ist (sonst kein Lock-Handling möglich).
static bool hasLockRoi() {
  static const char* lockNeedles[] = { "lock" };
  return findRoiIdByLabel(lockNeedles, 1).length() > 0;
}

// Schaltet die Tastensperre verifiziert auf wantLocked. Togglet per
// lock-Shortcut (Up+Down ~5 s) und prüft per OCR, ob der Zustand passt.
// Max. 2 Versuche. Rückgabe true = Zielzustand erreicht.
static const int LOCK_TOGGLE_ATTEMPTS = 2;
static const int LOCK_SETTLE_MS       = 1400;   // nach Toggle setteln lassen

static bool setLockState(bool wantLocked) {
  if (isDisplayLocked() == wantLocked) return true;   // schon im Zielzustand

  for (int attempt = 0; attempt < LOCK_TOGGLE_ATTEMPTS; attempt++) {
    // lock-Shortcut = Up+Down gleichzeitig ~5 s (toggelt die Sperre)
    String combo[2] = { "up", "down" };
    WpTouch.combo(combo, 2, 5300);
    delay(LOCK_SETTLE_MS);

    if (isDisplayLocked() == wantLocked) return true;
  }
  return false;   // Zielzustand nach allen Versuchen nicht erreicht
}

// Prüft, ob die WP im Standby ist: Soll-Temperatur nicht lesbar (-1), aber
// Ist-Temperatur gültig. (Bei komplett dunklem Display sind beide -1 → kein
// Standby in diesem Sinne, sondern "nicht lesbar".)
static bool isStandby() {
  static const char* sollNeedles[] = { "soll", "set", "target" };
  static const char* istNeedles[]  = { "ist", "water", "actual" };
  String sollId = findRoiIdByLabel(sollNeedles, 3);
  String istId  = findRoiIdByLabel(istNeedles, 3);
  if (sollId.length() == 0) return false;
  int soll = readRoiIntValue(sollId);
  int ist  = readRoiIntValue(istId);
  return (soll < 0 && ist >= 0);
}

// Weckt die WP aus dem Standby (wantAwake=true) bzw. schickt sie wieder hinein
// (wantAwake=false). Toggle = standby-Shortcut (Power ~3 s). Verifiziert per
// OCR: aufgeweckt = Soll wieder lesbar; eingeschlafen = Soll wieder -1.
// Max. 2 Versuche.
static const int STANDBY_TOGGLE_ATTEMPTS = 2;
static const int STANDBY_SETTLE_MS       = 1500;   // WP braucht nach Power kurz

static bool setStandbyState(bool wantAwake) {
  // Zielzustand schon erreicht?
  if (wantAwake && !isStandby()) return true;
  if (!wantAwake && isStandby()) return true;

  for (int attempt = 0; attempt < STANDBY_TOGGLE_ATTEMPTS; attempt++) {
    // standby-Shortcut = Power ~3 s (toggelt Standby/Betrieb)
    WpTouch.longPress("power", 3300);
    delay(STANDBY_SETTLE_MS);

    if (wantAwake && !isStandby()) return true;
    if (!wantAwake && isStandby()) return true;
  }
  return false;
}

// Closed-Loop: stellt die Solltemperatur auf target ein.
// Ergebnis-JSON wird in resultJson geschrieben. Rückgabe true = Erfolg.
static const int SET_TEMP_MAX_ITER   = 15;     // Sicherheitslimit
static const int SET_TEMP_SETTLE_MS  = 900;    // Wartezeit Druck→Re-Read
static const int SET_TEMP_COARSE_MIN = 3;      // ab dieser Differenz Grobsprung
static const int SET_TEMP_COARSE_SETTLE_MS = 1200;  // Settle nach Grob-Burst

// Interne Kern-Routine: stellt die Solltemperatur ein, OHNE Lock-Check
// (der Aufrufer stellt sicher, dass das Display entsperrt/wach ist).
static bool doSetSollCore(int target, String &resultJson) {
  // Soll-ROI finden (nur über Label)
  static const char* sollNeedles[] = { "soll", "set", "target" };
  String sollId = findRoiIdByLabel(sollNeedles, 3);
  if (sollId.length() == 0) {
    resultJson = "{\"ok\":false,\"error\":\"no_soll_roi\"}";
    return false;
  }

  // Aktuellen Soll-Wert lesen (stabil/entprellt)
  int current = readRoiStable(sollId);

  // Standby-Check: Soll nicht lesbar, aber Ist-Temp gültig → Standby
  if (current < 0) {
    static const char* istNeedles[] = { "ist", "water", "actual" };
    String istId = findRoiIdByLabel(istNeedles, 3);
    int istVal = readRoiIntValue(istId);
    if (istVal >= 0) {
      resultJson = "{\"ok\":false,\"error\":\"standby\"}";
      return false;
    }
    // sonst: Display generell nicht lesbar
    resultJson = "{\"ok\":false,\"error\":\"soll_unreadable\"}";
    return false;
  }

  // ── Grobsprung: bei großer Differenz zunächst per Mehrfachdruck-Burst nahe
  // ans Ziel springen (Differenz − 1, also 1 Grad Feinschliff-Puffer). Spart
  // viele Einzel-Lese-Zyklen. Der verschluckte erste Druck wird kompensiert
  // (burst + 1). Der anschließende Feinschliff (Schleife unten) korrigiert den
  // Rest verifiziert – auch ein eventuelles Überschießen in Gegenrichtung.
  bool didAdjust = false;
  int diff = target - current;
  int absDiff = (diff < 0) ? -diff : diff;
  if (absDiff >= SET_TEMP_COARSE_MIN) {
    int coarse = absDiff - 1;                 // 1 Grad Puffer für den Feinschliff
    const char* dir = (diff > 0) ? "up" : "down";
    WpTouch.press(dir, coarse + 1);           // +1: verschluckten ersten Druck ausgleichen
    delay(SET_TEMP_COARSE_SETTLE_MS);
    int after = readRoiStable(sollId);
    if (after >= 0) current = after;          // neuen Stand übernehmen
    didAdjust = true;
  }

  // Regelschleife (Feinschliff)
  bool retried = false;
  for (int iter = 0; iter < SET_TEMP_MAX_ITER; iter++) {
    if (current == target) {
      // Zielwert erreicht: einzelner "Power"-Druck schließt die Eingabe sofort
      // ab (sonst übernimmt die WP den Wert erst nach Eingabe-Timeout).
      // Nur wenn tatsächlich verstellt wurde – war der Wert schon korrekt,
      // ist die WP nicht im Einstellmodus und ein Power-Druck wäre unerwünscht.
      if (didAdjust) WpTouch.press("power", 1);
      resultJson = String("{\"ok\":true,\"set_temp\":") + target +
                   ",\"value\":" + current + "}";
      return true;
    }

    didAdjust = true;

    const char* dir = (target > current) ? "up" : "down";
    WpTouch.press(dir, 1);                 // einzelner verifizierter Druck
    delay(SET_TEMP_SETTLE_MS);

    int next = readRoiStable(sollId);

    if (next == current) {
      // Keine Änderung trotz Druck → einmal erneut versuchen
      if (!retried) {
        retried = true;
        WpTouch.press(dir, 1);
        delay(SET_TEMP_SETTLE_MS);
        next = readRoiStable(sollId);
      }
      if (next == current) {
        // Immer noch keine Änderung → Endanschlag / reagiert nicht
        resultJson = String("{\"ok\":false,\"error\":\"no_change\",\"value\":") +
                     current + "}";
        return false;
      }
    }

    if (next >= 0) {
      current = next;
      retried = false;   // erfolgreicher Schritt → Retry-Zähler zurücksetzen
    }
  }

  // Iterationslimit erreicht ohne Ziel
  resultJson = String("{\"ok\":false,\"error\":\"max_iterations\",\"value\":") +
               current + "}";
  return false;
}

// Öffentliche Funktion: stellt die Solltemperatur auf target ein.
// auto=false: bei gesperrtem Display Abbruch mit display_locked (Altverhalten).
// auto=true : entsperrt UND/ODER weckt bei Bedarf auf, stellt ein, und stellt
//             den Ausgangszustand wieder her (jeweils verifiziert).
bool setSollTemperatureEx(int target, bool autoHandle, String &resultJson) {
  bool wasLocked  = isDisplayLocked();

  // ── Ohne auto: bei Sperre abbrechen (bisheriges Verhalten) ───────────────
  if (wasLocked && !autoHandle) {
    resultJson = "{\"ok\":false,\"error\":\"display_locked\"}";
    return false;
  }

  // ── Mit auto: bei Bedarf entsperren (zuerst, Lock blockiert alles) ───────
  bool weUnlocked = false;
  if (wasLocked && autoHandle) {
    if (!hasLockRoi()) {
      resultJson = "{\"ok\":false,\"error\":\"no_lock_roi\"}";
      return false;
    }
    if (!setLockState(false)) {
      resultJson = "{\"ok\":false,\"error\":\"unlock_failed\"}";
      return false;   // nichts verändert
    }
    weUnlocked = true;
  }

  // ── Standby prüfen (erst nach dem Entsperren sinnvoll lesbar) ─────────────
  bool wasStandby = isStandby();
  bool weWokeUp = false;
  if (wasStandby && !autoHandle) {
    // Ohne auto: Standby melden (und ggf. wieder sperren, falls wir entsperrt
    // hätten – hier sind wir aber im !auto-Zweig, also wasLocked war false)
    resultJson = "{\"ok\":false,\"error\":\"standby\"}";
    return false;
  }
  if (wasStandby && autoHandle) {
    if (!setStandbyState(true)) {   // aufwecken
      // Aufwecken fehlgeschlagen → ggf. Entsperrung rückgängig machen
      String restore;
      if (weUnlocked) setLockState(true);
      resultJson = "{\"ok\":false,\"error\":\"wake_failed\"}";
      return false;
    }
    weWokeUp = true;
  }

  // ── Kern: Temperatur einstellen ──────────────────────────────────────────
  bool ok = doSetSollCore(target, resultJson);

  // ── Restore: erst einschlafen, dann sperren ─────────────────────────────
  // (An dieser WP lässt sich auch im Standby noch sperren. Würde man zuerst
  //  sperren, blockierte die Sperre das Einschlafen.)
  bool reslept   = true;
  bool relocked  = true;
  if (weWokeUp)   reslept  = setStandbyState(false);   // zuerst wieder einschlafen
  if (weUnlocked) relocked = setLockState(true);       // dann wieder sperren

  // ── Info-Felder ans resultJson hängen ────────────────────────────────────
  if ((weUnlocked || weWokeUp) && resultJson.endsWith("}")) {
    resultJson.remove(resultJson.length() - 1);
    if (weUnlocked) {
      resultJson += String(",\"auto_unlocked\":true,\"relocked\":") +
                    (relocked ? "true" : "false");
    }
    if (weWokeUp) {
      resultJson += String(",\"auto_woken\":true,\"reslept\":") +
                    (reslept ? "true" : "false");
    }
    resultJson += "}";
  }
  if (!relocked) LOGE("auto: re-lock failed after set_temp!\n");
  if (!reslept)  LOGE("auto: re-standby failed after set_temp!\n");

  return ok;
}

// Kompatibilitäts-Wrapper (altes Verhalten ohne auto).
bool setSollTemperature(int target, String &resultJson) {
  return setSollTemperatureEx(target, false, resultJson);
}

// Relativ-mit-Verifikation: liest aktuellen Soll, berechnet Ziel = aktuell +
// delta (delta kann negativ sein) und nutzt den Closed-Loop. Für
// {"button":"up","count":N,"verify":true} (delta=+N) bzw. "down" (delta=-N).
bool setSollRelativeEx(int delta, bool autoHandle, String &resultJson) {
  bool wasLocked = isDisplayLocked();

  if (wasLocked && !autoHandle) {
    resultJson = "{\"ok\":false,\"error\":\"display_locked\"}";
    return false;
  }

  bool weUnlocked = false;
  if (wasLocked && autoHandle) {
    if (!hasLockRoi()) {
      resultJson = "{\"ok\":false,\"error\":\"no_lock_roi\"}";
      return false;
    }
    if (!setLockState(false)) {
      resultJson = "{\"ok\":false,\"error\":\"unlock_failed\"}";
      return false;
    }
    weUnlocked = true;
  }

  // Standby prüfen / bei auto aufwecken
  bool wasStandby = isStandby();
  bool weWokeUp = false;
  if (wasStandby && autoHandle) {
    if (!setStandbyState(true)) {
      if (weUnlocked) setLockState(true);
      resultJson = "{\"ok\":false,\"error\":\"wake_failed\"}";
      return false;
    }
    weWokeUp = true;
  }

  // Aktuellen Soll lesen (jetzt entsperrt + wach → lesbar)
  static const char* sollNeedles[] = { "soll", "set", "target" };
  String sollId = findRoiIdByLabel(sollNeedles, 3);
  bool ok = false;
  if (sollId.length() == 0) {
    resultJson = "{\"ok\":false,\"error\":\"no_soll_roi\"}";
  } else {
    int current = readRoiIntValue(sollId);
    if (current < 0) {
      static const char* istNeedles[] = { "ist", "water", "actual" };
      int istVal = readRoiIntValue(findRoiIdByLabel(istNeedles, 3));
      resultJson = (istVal >= 0)
        ? String("{\"ok\":false,\"error\":\"standby\"}")
        : String("{\"ok\":false,\"error\":\"soll_unreadable\"}");
    } else {
      ok = doSetSollCore(current + delta, resultJson);
    }
  }

  // Restore: erst einschlafen, dann sperren (Sperre geht auch im Standby)
  bool reslept  = true;
  bool relocked = true;
  if (weWokeUp)   reslept  = setStandbyState(false);
  if (weUnlocked) relocked = setLockState(true);

  if ((weUnlocked || weWokeUp) && resultJson.endsWith("}")) {
    resultJson.remove(resultJson.length() - 1);
    if (weUnlocked) {
      resultJson += String(",\"auto_unlocked\":true,\"relocked\":") +
                    (relocked ? "true" : "false");
    }
    if (weWokeUp) {
      resultJson += String(",\"auto_woken\":true,\"reslept\":") +
                    (reslept ? "true" : "false");
    }
    resultJson += "}";
  }
  if (!relocked) LOGE("auto: re-lock failed after verify-set!\n");
  if (!reslept)  LOGE("auto: re-standby failed after verify-set!\n");

  return ok;
}

// Kompatibilitäts-Wrapper (altes Verhalten ohne auto).
bool setSollRelative(int delta, String &resultJson) {
  return setSollRelativeEx(delta, false, resultJson);
}

// Liefert den aktuell ausgelesenen Soll-Wert (entprellt) oder -1, wenn er nicht
// lesbar ist (z. B. Standby oder gesperrt). Für das Live-Popup, um die Stepper-
// Anzeige beim Öffnen mit dem echten Wert vorzubelegen.
int readCurrentSoll() {
  static const char* sollNeedles[] = { "soll", "set", "target" };
  String sollId = findRoiIdByLabel(sollNeedles, 3);
  if (sollId.length() == 0) return -1;
  return readRoiStable(sollId);
}

// ───────────────────────── Uhrzeit setzen (Closed-Loop) ────────────────────
// Strategie: vorher lesen (stabiler Normalmodus) → blind setzen (Stunden und
// Minuten getrennt, im Einstellmodus wird NICHT gelesen, da dort geblinkt wird)
// → hinterher kontrollieren → bei Abweichung weiterer Durchgang (max. 3).
// Kein verschluckter erster Druck im Uhrzeit-Modus (anders als bei Temp).

static const int SET_TIME_MAX_PASSES = 3;     // Durchgänge bis Abbruch
static const int SET_TIME_STEP_MS    = 350;   // Pause zwischen Einzeldrücken
static const int SET_TIME_MODE_MS    = 600;   // Pause nach Time-Moduswechsel
static const int SET_TIME_SETTLE_MS  = 1200;  // Pause vor Kontroll-Lesung

// Findet das Uhrzeit-ROI (Label enthält uhr/zeit/time/clock).
static String findTimeRoiId() {
  static const char* needles[] = { "uhr", "zeit", "time", "clock" };
  return findRoiIdByLabel(needles, 4);
}

// Liest die aktuelle Uhrzeit als HHMM-Zahl (z. B. 1305) oder -1.
static int readClockHHMM(const String &roiId) {
  int v = readRoiStable(roiId);
  if (v < 0 || v > 2359) return -1;
  return v;
}

// Kürzeste-Richtung-Schritte für zyklische Werte (mod modN).
// Rückgabe: signierte Schrittzahl (positiv = up, negativ = down).
static int shortestSteps(int from, int to, int modN) {
  int diff = ((to - from) % modN + modN) % modN;   // 0..modN-1 (hoch)
  if (diff <= modN / 2) return diff;                // hoch kürzer/gleich
  return diff - modN;                               // runter kürzer (negativ)
}

// Drückt n-mal up (n>0) bzw. down (n<0), einzeln mit Pause. Ohne Kompensation.
static void pressSteps(int n) {
  const char* dir = (n >= 0) ? "up" : "down";
  int cnt = (n >= 0) ? n : -n;
  for (int i = 0; i < cnt; i++) {
    WpTouch.press(dir, 1);
    delay(SET_TIME_STEP_MS);
  }
}

// Setzt die Uhrzeit auf targetH:targetM. Ergebnis-JSON in resultJson.
// Interne Kern-Routine: setzt die Uhrzeit, OHNE Lock/Standby-Handling
// (der Aufrufer stellt sicher, dass das Display entsperrt und wach ist).
static bool doSetClockCore(int targetH, int targetM, String &resultJson) {
  String roiId = findTimeRoiId();
  if (roiId.length() == 0) {
    resultJson = "{\"ok\":false,\"error\":\"no_time_roi\"}";
    return false;
  }
  if (targetH < 0 || targetH > 23 || targetM < 0 || targetM > 59) {
    resultJson = "{\"ok\":false,\"error\":\"bad_time\"}";
    return false;
  }

  for (int pass = 1; pass <= SET_TIME_MAX_PASSES; pass++) {
    // 1. Startwert lesen (stabiler Normalmodus)
    int cur = readClockHHMM(roiId);
    if (cur < 0) {
      resultJson = "{\"ok\":false,\"error\":\"time_unreadable\"}";
      return false;
    }
    int curH = cur / 100;
    int curM = cur % 100;

    // 2. Differenzen (kürzeste Richtung, zyklisch)
    int dH = shortestSteps(curH, targetH, 24);
    int dM = shortestSteps(curM, targetM, 60);

    // Schon korrekt?
    if (dH == 0 && dM == 0) {
      char buf[64];
      snprintf(buf, sizeof(buf),
        "{\"ok\":true,\"set_time\":\"%02d:%02d\",\"value\":\"%02d:%02d\",\"passes\":%d}",
        targetH, targetM, curH, curM, pass);
      resultJson = buf;
      return true;
    }

    // 3. Blind setzen: Time → Stunden → Time → Minuten → Time
    WpTouch.press("time", 1);
    delay(SET_TIME_MODE_MS);
    pressSteps(dH);

    WpTouch.press("time", 1);
    delay(SET_TIME_MODE_MS);
    pressSteps(dM);

    WpTouch.press("time", 1);          // beenden/speichern
    delay(SET_TIME_SETTLE_MS);

    // 4. Kontrolle erfolgt im nächsten Schleifendurchlauf (erneutes Lesen).
    // Wenn das der letzte Pass war, abschließend prüfen:
    if (pass == SET_TIME_MAX_PASSES) {
      int after = readClockHHMM(roiId);
      int aH = (after >= 0) ? after / 100 : -1;
      int aM = (after >= 0) ? after % 100 : -1;
      bool okFinal = (aH == targetH && aM == targetM);
      char buf[96];
      snprintf(buf, sizeof(buf),
        "{\"ok\":%s,\"set_time\":\"%02d:%02d\",\"value\":\"%02d:%02d\",\"passes\":%d%s}",
        okFinal ? "true" : "false", targetH, targetM,
        (aH >= 0 ? aH : 0), (aM >= 0 ? aM : 0), pass,
        okFinal ? "" : ",\"error\":\"not_reached\"");
      resultJson = buf;
      return okFinal;
    }
  }

  // (theoretisch nicht erreichbar)
  resultJson = "{\"ok\":false,\"error\":\"max_passes\"}";
  return false;
}

// Öffentliche Funktion mit optionalem Lock/Standby-Auto-Handling.
// auto=false: bei Sperre/Standby Abbruch mit display_locked/standby.
// auto=true : entsperrt + weckt bei Bedarf auf, stellt die Zeit, und stellt den
//             Ausgangszustand wieder her (analog zur Solltemperatur).
bool setClockTimeEx(int targetH, int targetM, bool autoHandle, String &resultJson) {
  if (targetH < 0 || targetH > 23 || targetM < 0 || targetM > 59) {
    resultJson = "{\"ok\":false,\"error\":\"bad_time\"}";
    return false;
  }

  bool wasLocked = isDisplayLocked();

  if (wasLocked && !autoHandle) {
    resultJson = "{\"ok\":false,\"error\":\"display_locked\"}";
    return false;
  }

  // Mit auto: bei Bedarf entsperren (zuerst, Lock blockiert alles)
  bool weUnlocked = false;
  if (wasLocked && autoHandle) {
    if (!hasLockRoi()) {
      resultJson = "{\"ok\":false,\"error\":\"no_lock_roi\"}";
      return false;
    }
    if (!setLockState(false)) {
      resultJson = "{\"ok\":false,\"error\":\"unlock_failed\"}";
      return false;
    }
    weUnlocked = true;
  }

  // Standby prüfen / bei auto aufwecken
  bool wasStandby = isStandby();
  bool weWokeUp = false;
  if (wasStandby && !autoHandle) {
    resultJson = "{\"ok\":false,\"error\":\"standby\"}";
    return false;
  }
  if (wasStandby && autoHandle) {
    if (!setStandbyState(true)) {
      if (weUnlocked) setLockState(true);
      resultJson = "{\"ok\":false,\"error\":\"wake_failed\"}";
      return false;
    }
    weWokeUp = true;
  }

  // Kern: Uhrzeit stellen
  bool ok = doSetClockCore(targetH, targetM, resultJson);

  // Restore: erst einschlafen, dann sperren (Sperre geht auch im Standby)
  bool reslept  = true;
  bool relocked = true;
  if (weWokeUp)   reslept  = setStandbyState(false);
  if (weUnlocked) relocked = setLockState(true);

  // Info-Felder ans resultJson hängen
  if ((weUnlocked || weWokeUp) && resultJson.endsWith("}")) {
    resultJson.remove(resultJson.length() - 1);
    if (weUnlocked) {
      resultJson += String(",\"auto_unlocked\":true,\"relocked\":") +
                    (relocked ? "true" : "false");
    }
    if (weWokeUp) {
      resultJson += String(",\"auto_woken\":true,\"reslept\":") +
                    (reslept ? "true" : "false");
    }
    resultJson += "}";
  }
  if (!relocked) LOGE("auto: re-lock failed after set_time!\n");
  if (!reslept)  LOGE("auto: re-standby failed after set_time!\n");

  return ok;
}

// Parst "HH:MM" und ruft setClockTimeEx. Für MQTT {"set_time":"14:45"}.
bool setClockTimeStr(const String &hhmm, bool autoHandle, String &resultJson) {
  int colon = hhmm.indexOf(':');
  if (colon < 1) {
    resultJson = "{\"ok\":false,\"error\":\"bad_format\"}";
    return false;
  }
  int h = hhmm.substring(0, colon).toInt();
  int m = hhmm.substring(colon + 1).toInt();
  return setClockTimeEx(h, m, autoHandle, resultJson);
}

static String evaluateMedian3State(AppConfig &cfg) {
  JsonDocument d1, d2, d3;

  bool ok1 = acquireStateFrame(d1);
  server.handleClient();   // Touch/Web-Requests zwischendurch bedienen
  delay(30);

  bool ok2 = acquireStateFrame(d2);
  server.handleClient();
  delay(30);

  bool ok3 = acquireStateFrame(d3);
  server.handleClient();


  JsonDocument out;
  out["valid"] = true;

  for (const auto &roi : cfg.rois) {
    if (roi.type == "symbol") {
      bool v1 = ok1 ? (d1[roi.id] | false) : false;
      bool v2 = ok2 ? (d2[roi.id] | false) : false;
      bool v3 = ok3 ? (d3[roi.id] | false) : false;

      bool v = majority3Bool(v1, v2, v3);
      out[roi.id] = v;

      LOGD("median symbol roi=%s values=%d,%d,%d -> %d\n",
           roi.id.c_str(), v1, v2, v3, v);
    } else if (roi.type == "sevenseg") {
      double v1 = ok1 ? (d1[roi.id] | -1.0) : -1.0;
      double v2 = ok2 ? (d2[roi.id] | -1.0) : -1.0;
      double v3 = ok3 ? (d3[roi.id] | -1.0) : -1.0;

      double validVals[3];
      int count = 0;
      if (v1 >= 0) validVals[count++] = v1;
      if (v2 >= 0) validVals[count++] = v2;
      if (v3 >= 0) validVals[count++] = v3;

      if (count == 0) {
        out["valid"] = false;
        out[roi.id] = -1;
        LOGD("median sevenseg roi=%s values=%f,%f,%f -> invalid\n",
             roi.id.c_str(), v1, v2, v3);
      } else if (count == 1) {
        if (roi.decimal_places > 0) {
          out[roi.id] = validVals[0];
        } else {
          out[roi.id] = (int)round(validVals[0]);
        }
      } else if (count == 2) {
        double v = (validVals[0] + validVals[1]) / 2.0;
        if (roi.decimal_places > 0) {
          out[roi.id] = v;
        } else {
          out[roi.id] = (int)round(v);
        }
      } else {
        double v = median3Double(validVals[0], validVals[1], validVals[2]);
        if (roi.decimal_places > 0) {
          out[roi.id] = v;
        } else {
          out[roi.id] = (int)round(v);
        }
      }
    }
  }

  String payload;
  serializeJson(out, payload);
  return payload;
}

static bool payloadIsValidState(const String &payload) {
  JsonDocument doc;

  if (deserializeJson(doc, payload)) return false;

  return doc["valid"] | false;
}

static void maybeRestartCamera() {
  if (cameraCaptureFailureCount() >= CAMERA_RESTART_AFTER_FAILS) {
    LOGI("Camera watchdog triggered after %lu capture failures\n",
         cameraCaptureFailureCount());

    if (cameraRestart(cfg)) {
      LOGI("Camera restart successful\n");
    } else {
      LOGE("Camera restart failed\n");
    }
  }
}

static uint16_t mqttBufferSizeForConfig(const AppConfig &cfg) {
  return cfg.mqtt.image_enabled ? MQTT_BUFFER_SIZE_WITH_IMAGE : MQTT_BUFFER_SIZE_DEFAULT;
}

static void maybePublishMqttImage(PubSubClient &mqtt, const AppConfig &cfg) {
  if (!cfg.mqtt.enabled || !cfg.mqtt.image_enabled) return;
  camera_fb_t *fb = cameraCapture();
  if (!fb) return;
  // Gleiche Feinrotation wie bei der OCR/Live-Ansicht anwenden, damit der
  // MQTT-Snapshot ebenfalls gerade ausgerichtet ist.
  rotateGrayscaleFb(fb, cfg.camera.fine_rotation);
  uint8_t *jpg_buf = nullptr; size_t jpg_len = 0;
  bool ok = cameraToJpegScaled(fb, 40, &jpg_buf, &jpg_len);
  cameraRelease(fb);
  if (!ok || !jpg_buf) { if (jpg_buf) free(jpg_buf); return; }
  mqttPublishImage(mqtt, cfg, jpg_buf, jpg_len);
  free(jpg_buf);
}


void setup() {
  Serial.begin(115200);
  delay(2000);  // Längerer Delay für stabilen PSRAM-Start

  LOGI("Booting HP-Remote\n");
#ifdef FW_VERSION
  LOGI("Firmware: %s\n", FW_VERSION);
#endif
#ifdef FW_BUILD
  LOGI("Build: %s\n", FW_BUILD);
#endif
  // OTA Rollback-Schutz nur wenn wir wirklich von OTA gestartet sind
  const esp_partition_t* running_check = esp_ota_get_running_partition();
  if (running_check && running_check->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
    esp_ota_mark_app_valid_cancel_rollback();
  }

  const esp_partition_t *running = esp_ota_get_running_partition();
  if (running) {
    LOGI("Running partition: label=%s subtype=%d address=0x%08x size=%u\n",
         running->label,
         running->subtype,
         (unsigned)running->address,
         (unsigned)running->size);
  }

  const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
  if (next) {
    LOGI("Next OTA partition: label=%s subtype=%d address=0x%08x size=%u\n",
         next->label,
         next->subtype,
         (unsigned)next->address,
         (unsigned)next->size);
  }

  // Mount LittleFS before loadDefaultConfig so devicePersistentUid() is available.
  if (!storageBegin()) {
    LOGE("LittleFS mount failed\n");
  } else {
    LOGI("LittleFS mounted\n");
  }

  loadDefaultConfig(cfg);

  loadConfigFromFile(cfg);

  // Migration: alte Geräte-IDs (wp_remote / 000000) auf neue Defaults zurücksetzen
  if (cfg.device.id.indexOf("wp_remote") >= 0 ||
      cfg.device.id.indexOf("000000")    >= 0 ||
      cfg.device.id.length() == 0) {
    LOGI("Migrating legacy device id '%s' -> defaults\n", cfg.device.id.c_str());
    cfg.device.id   = defaultDeviceId();
    cfg.device.name = defaultDeviceName();
    cfg.wifi.hostname = defaultHostname();
    // MQTT-Topic nur migrieren wenn es noch das alte Format hat
    if (cfg.mqtt.topic.indexOf("wp_remote") >= 0 ||
        cfg.mqtt.topic.indexOf("000000")    >= 0) {
      cfg.mqtt.topic = defaultMqttTopicBase();
    }
    saveConfigToFile(cfg);
  }

  LOGI("Debug level: %d\n", cfg.debug_level);


  wifiState = wifiBegin(cfg);

  mqttApplyConfig(mqtt, cfg);
  mqttSetGlobalClient(&mqtt);
  uint16_t mqttBufferSize = mqttBufferSizeForConfig(cfg);
  mqtt.setBufferSize(mqttBufferSize);

  // Keepalive großzügig (120s): Lange Closed-Loop-Vorgänge mit Auto-Handling
  // (set_time/set_temp mit auto: entsperren → aufwecken → stellen → einschlafen
  // → sperren, mit mehreren Durchgängen) können das Gerät über eine Minute
  // blockieren. In dieser Zeit läuft mqtt.loop() nicht – der Keepalive muss
  // länger sein als der längstmögliche Vorgang, sonst trennt der Broker.
  mqtt.setKeepAlive(120);

  LOGI("MQTT buffer size set to %u\n", mqttBufferSize);

  LOGI("MQTT config applied: host=%s port=%u enabled=%d\n",
       cfg.mqtt.host.c_str(),
       cfg.mqtt.port,
       cfg.mqtt.enabled);

  // Kamera wird beim ersten loop()-Durchlauf initialisiert
  // (nach WiFi/PSRAM vollständig bereit)

  // ── WP-Remote: Touch-Controller initialisieren ──────────────────────────
  WpTouch.begin();
  mqttRegisterTouchConfig(&cfg);

  // ── Heizraum-Sensor initialisieren (SHT31, optional, gemeinsamer I2C-Bus) ─
  RoomSensorSht31.begin();

  // MQTT Callback für Touch-Commands registrieren
  // Kombiniert mit bestehendem Pre-Capture Callback über Lambda-Wrapper
  mqtt.setCallback([](const char* topic, byte* payload, unsigned int len) {
      String t(topic);
      // Leitet Touch-Commands an den Touch-Handler weiter
      if (t.endsWith(WP_MQTT_TOPIC_CMD_SUFFIX)) {
          mqttTouchCallback(topic, payload, len);
      }
  });

  if (wifiState == WIFI_MODE_STA_OK) {
    otaBegin();
  } else {
    LOGI("OTA disabled in AP mode\n");
  }

  setupWebServer(server, mqtt, cfg, lastPayload);
  mjpegServerBegin();

  LOGI("HTTP server started\n");
}

static bool s_cameraStarted = false;

void loop() {
  // Kamera beim ersten loop()-Aufruf starten (PSRAM und WiFi sind dann bereit)
  if (!s_cameraStarted) {
    s_cameraStarted = true;
    if (!cameraStart(cfg)) {
      LOGE("Camera start FAILED – check OV2640 FPC cable\n");
    } else {
      LOGI("Camera OK: %s\n", cameraSensorName());
    }
  }

  server.handleClient();
  mjpegServerHandle();
  mqtt.loop();

  if (wifiState == WIFI_MODE_STA_OK) {
    otaHandle();
  }

  unsigned long now = millis();
  unsigned long intervalMs = (unsigned long)cfg.capture_interval_sec * 1000UL;

  // Periodische OCR-Auswertung nur wenn MQTT aktiviert ist. Bei deaktiviertem
  // MQTT bleibt das Gerät ruhig (spart CPU, hält Web/Touch responsiv).
  // Manuelle Auswertung (Test Detection) und Live-Stream laufen unabhängig weiter.
  if (cfg.mqtt.enabled &&
      !g_liveStreamTriggerActive && now - lastEvalMs >= intervalMs) {
    lastEvalMs = now;
    String newPayload = runActiveEvaluation(cfg);
    if (payloadIsValidState(newPayload)) {
      lastPayload = newPayload;
      lastGoodPayload = newPayload;
    } else {
      lastPayload = (lastGoodPayload.length() > 2) ? lastGoodPayload : newPayload;
    }
    mqttPublishState(mqtt, cfg, lastPayload);
    maybePublishMqttImage(mqtt, cfg);
    maybeRestartCamera();
  }

  // Kamera-Status alle 30s im Serial-Monitor ausgeben (sichtbar nach Monitor-Connect)
  static unsigned long lastCamLogMs = 0;
  if (now - lastCamLogMs >= 30000UL) {
    lastCamLogMs = now;
    LOGI("Camera status: %s | sensor: %s\n",
         cameraIsHealthy() ? "OK" : "FAULT",
         cameraSensorName());
  }

  if (now - lastHealthMs >= HEALTH_INTERVAL_MS) {
    lastHealthMs = now;

    String health = buildHealthPayload();

    mqttPublishHealth(mqtt, cfg, health);

    LOGD("MQTT health payload: %s\n", health.c_str());
  }

  // ── Heizraum-Sensor (SHT31, optional) ──────────────────────────────────
  // Lesen UND Verfügbarkeitsprüfung sind ein gemeinsamer Vorgang: schlägt
  // die I2C-Transaktion fehl, gilt der Sensor als nicht erkannt. Das Lesen
  // läuft unabhängig von MQTT kontinuierlich im eingestellten Intervall
  // (auch für die Live-Anzeige im Web-UI-Status) – nur die Übertragung
  // (State/Discovery/Availability) ist an aktives MQTT gekoppelt.
  unsigned long roomIntervalMs = (unsigned long)cfg.room_sensor.read_interval_sec * 1000UL;

  if (!cfg.room_sensor.enabled) {
    if (roomSensorWasAvailable) {
      if (cfg.mqtt.enabled) mqttSetRoomSensorAvailability(mqtt, cfg, false);
      roomSensorWasAvailable = false;
    }
  } else if (now - lastRoomSensorMs >= roomIntervalMs) {
    lastRoomSensorMs = now;

    float roomTempC, roomHumidityPct;
    if (RoomSensorSht31.read(cfg.room_sensor.temp_offset, cfg.room_sensor.humidity_offset,
                              roomTempC, roomHumidityPct)) {
      if (cfg.mqtt.enabled) {
        JsonDocument roomDoc;
        roomDoc["room_temp_c"] = roomTempC;
        roomDoc["room_humidity_pct"] = roomHumidityPct;
        String roomPayload;
        serializeJson(roomDoc, roomPayload);

        mqttPublishRoomSensorState(mqtt, cfg, roomPayload);

        if (!roomSensorWasAvailable) {
          LOGI("Heizraum-Sensor erkannt – HA-Discovery wird gesendet\n");
          mqttPublishRoomSensorDiscovery(mqtt, cfg);
        }
      }
      roomSensorWasAvailable = true;
    } else if (roomSensorWasAvailable) {
      LOGI("Heizraum-Sensor nicht mehr erreichbar\n");
      if (cfg.mqtt.enabled) mqttSetRoomSensorAvailability(mqtt, cfg, false);
      roomSensorWasAvailable = false;
    }
  }
}