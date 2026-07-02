#include "web_server.h"
#include "build_info.h"

#include <WiFi.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <cstring>

#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "debug_log.h"
#include "storage_service.h"
#include "wifi_manager.h"
#include "camera_service.h"
#include "analyzer.h"
#include "mqtt_service.h"
#include "web_ui.h"
#include "touch_controller.h"
#include "room_sensor.h"

#include "mqtt_ha_discovery.h"

static bool g_updateRunning = false;
static size_t g_updateTotal = 0;
static size_t g_updateWritten = 0;
static bool g_updateOk = false;

bool g_liveStreamTriggerActive = false;
static String g_updateError = "";
static const esp_partition_t *g_updateTargetPartition = nullptr;

// In main.cpp: liefert aktuellen Soll-Wert (entprellt) oder -1.
extern int readCurrentSoll();

// In mqtt_service.cpp: Ergebnis des zuletzt verarbeiteten Touch-Befehls.
extern String g_lastTouchResult;
static const uint16_t MQTT_BUFFER_SIZE_DEFAULT = 2048;
static const uint16_t MQTT_BUFFER_SIZE_WITH_IMAGE = 32768;

static uint16_t mqttBufferSizeForConfig(const AppConfig &cfg) {
  return cfg.mqtt.image_enabled ? MQTT_BUFFER_SIZE_WITH_IMAGE : MQTT_BUFFER_SIZE_DEFAULT;
}


// ── MJPEG Stream Server (Port 81) ────────────────────────────────────────────
static WiFiServer mjpegServer(81);
static WiFiClient mjpegClient;

void mjpegServerBegin() {
  mjpegServer.begin();
  LOGI("MJPEG stream server on port 81\n");
}

void mjpegServerHandle() {
  // Neuen Client annehmen
  if (mjpegServer.hasClient()) {
    if (mjpegClient && mjpegClient.connected()) {
      mjpegClient.stop();
    }
    mjpegClient = mjpegServer.accept();
    LOGI("MJPEG client connected\n");

    // TCP-Optimierung: Nagle aus → Frames werden sofort gesendet (weniger Ruckeln)
    mjpegClient.setNoDelay(true);

    // HTTP Header senden
    mjpegClient.println("HTTP/1.1 200 OK");
    mjpegClient.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
    mjpegClient.println("Access-Control-Allow-Origin: *");
    mjpegClient.println("Cache-Control: no-store");
    mjpegClient.println();
  }

  // Frame an verbundenen Client senden
  if (!mjpegClient || !mjpegClient.connected()) {
    g_liveStreamTriggerActive = false;   // Kein Client → OCR-Auswertung wieder erlauben
    return;
  }
  if (!cameraIsHealthy()) return;

  // Solange ein Stream-Client verbunden ist, periodische OCR-Auswertung pausieren
  // (verhindert Ruckeln durch die teure Median-3-Analyse während des Justierens).
  g_liveStreamTriggerActive = true;

  // Mehrere Frames pro Loop senden (Zeitbudget), damit der Stream flüssiger
  // läuft und nicht von der Loop-Frequenz limitiert wird.
  const unsigned long budgetMs = 40;          // max. ~40ms pro Loop für Stream
  const unsigned long tStart   = millis();

  do {
    camera_fb_t *fb = cameraCaptureFast();
    if (!fb) return;

    uint8_t *jpg = nullptr;
    size_t   len = 0;
    // Grayscale → JPEG, auf halbe Auflösung skaliert (2x2-Mittelung).
    // Viertelt die Pixelzahl → deutlich kleinere Frames, flüssigerer Stream.
    // Für ROI-Justage völlig ausreichend.
    bool ok = cameraToJpegScaled(fb, 15, &jpg, &len);
    cameraRelease(fb);

    if (!ok || !jpg) { if (jpg) free(jpg); return; }

    // Frame senden
    mjpegClient.print("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: ");
    mjpegClient.print(len);
    mjpegClient.print("\r\n\r\n");
    size_t written = mjpegClient.write(jpg, len);
    mjpegClient.print("\r\n");
    free(jpg);

    // Wenn der Client-Puffer voll ist (write unvollständig), abbrechen
    if (written != len) break;

    yield();   // WiFi/TCP-Stack bedienen
  } while (mjpegClient.connected() && (millis() - tStart) < budgetMs);
}

void setupWebServer(WebServer &server, PubSubClient &mqtt, AppConfig &cfg, String &lastPayload) {
  server.on("/", HTTP_GET, [&]() {
    LOGD("HTTP GET /\n");
    String mode = wifiCurrentModeString();
    if (mode == "AP" || mode == "AP_STA") {
      server.send_P(200, "text/html", WIFI_SETUP_HTML);
    } else {
      server.send_P(200, "text/html", INDEX_HTML);
    }
  });

  // Favicon: SVG Thermometer-Icon (inline, kein Binär-Asset nötig)
  server.on("/favicon.ico", HTTP_GET, [&]() {
    static const char *FAVICON_SVG =
      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24'>"
      "<rect width='24' height='24' rx='5' fill='#1e88e5'/>"
      "<path fill='white' d='M15 13V5a3 3 0 0 0-6 0v8a5 5 0 1 0 6 0z'/>"
      "<circle cx='12' cy='17' r='2' fill='#1e88e5'/>"
      "</svg>";
    server.send(200, "image/svg+xml", FAVICON_SVG);
  });

  server.on("/live", HTTP_GET, [&]() {
    LOGD("HTTP GET /live\n");

    if (!cfg.stream.enabled) {
      server.send(403, "text/plain", "Stream disabled. Enable it in Camera settings.");
      return;
    }

    String html =
      "<!doctype html><html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width,initial-scale=1'>"
      "<title>HP-Remote Live</title>"
      "<style>"
      "*{box-sizing:border-box;margin:0;padding:0}"
      "body{background:#111;display:flex;flex-direction:column;height:100vh;"
           "font-family:sans-serif;overflow:hidden}"
      "#stream{flex:1;width:100%;object-fit:contain;display:block;min-height:0}"
      ".bar{display:flex;flex-direction:column;align-items:center;justify-content:center;"
           "padding:8px 10px;background:#1a1a20;border-top:1px solid #2a2a35;"
           "flex-shrink:0;position:relative}"
      ".btns{display:flex;flex-direction:column;gap:5px;align-items:center}"
      ".row{display:flex;gap:6px;justify-content:center}"
      ".row2{justify-content:center}"
      ".row3{justify-content:center;flex-wrap:wrap}"
      ".row3 button{font-size:14px}"
      ".row4{justify-content:center;align-items:center;gap:8px}"
      ".row5{justify-content:center;align-items:center;margin-top:5px;gap:6px}"
      ".row5 button{font-size:14px;width:auto;min-width:48px;padding:0 8px}"
      ".row1{justify-content:center;align-items:center;gap:6px;flex-wrap:wrap}"
      ".tg{width:auto;height:44px;font-size:12px;padding:0 7px;color:#aaa}"
      ".tg.open{background:#2a3550;border-color:#3a5580;color:#cde}"
      ".sect{display:flex;flex-direction:column;gap:5px;align-items:center;"
            "margin-top:5px;padding-top:8px;border-top:1px solid #2a2a35;"
            "width:100%}"
      ".sect.hidden{display:none}"
      "#tval{font-size:22px;color:#fff;min-width:64px;text-align:center;"
            "font-weight:600;font-variant-numeric:tabular-nums}"
      "#setbtn{background:#1a3a5a;border-color:#2a5a8a;font-weight:600}"
      "#status{position:absolute;right:10px;bottom:8px;font-size:15px;"
             "font-weight:600;color:#9cf}"
      "button{width:48px;height:44px;border-radius:9px;border:1px solid #3a3a4a;"
             "background:#25252f;color:#ddd;cursor:pointer;font-size:18px;"
             "display:flex;align-items:center;justify-content:center;"
             "transition:background .15s;-webkit-tap-highlight-color:transparent}"
      "button:active{background:#3a3a50}"
      "button.ok{background:#1a4a2a;border-color:#2a7a3a}"
      "button.err{background:#4a1a1a;border-color:#7a2a2a}"
      "#status{min-width:60px;text-align:right}"
      "#status.busy{font-size:17px;color:#fd6;"
                  "animation:pulse 1s ease-in-out infinite}"
      "@keyframes pulse{0%,100%{opacity:1}50%{opacity:.45}}"
      "</style></head><body>"
      "<img id='stream' alt='MJPEG Stream' style='transform:rotate(" + String(cfg.camera.fine_rotation, 1) + "deg)'>"
      "<div class='bar'>"
        "<div class='btns'>"
          "<div class='row row1'>"
            "<button id='tgScut' class='tg' onclick='toggle(\"scut\")' title='Shortcuts'>&#9889;&#9662;</button>"
            "<button onclick='press(\"power\")'   title='Power'>&#9211;</button>"
            "<button onclick='press(\"boost\")'   title='Boost/PV'>&#9889;</button>"
            "<button onclick='press(\"settings\")' title='Settings'>&#9881;</button>"
            "<button onclick='press(\"time\")'    title='Time'>&#128336;</button>"
            "<button id='tgTemp' class='tg' onclick='toggle(\"temp\")' title='Temperatur'>&#127777;&#9662;</button>"
          "</div>"
          "<div class='row row2'>"
            "<button onclick='press(\"up\")'   title='Temp+'>&#8743;</button>"
            "<button onclick='press(\"down\")'  title='Temp&minus;'>&#8744;</button>"
          "</div>"
          "<div id='scut' class='sect hidden'>"
            "<div class='row row3'>"
              "<button onclick='shortcut(\"standby\")' title='Standby (Power 3s)'>&#9211;&#9211;</button>"
              "<button onclick='shortcut(\"fan\")'     title='Fan vent (Boost 5s)'>&#10052;</button>"
              "<button onclick='shortcut(\"lock\")'    title='Key lock (Up+Down 5s)'>&#128274;</button>"
              "<button onclick='shortcut(\"timer\")'   title='Timer setup (Time 5s)'>&#128336;&#9881;</button>"
              "<button onclick='shortcut(\"params\")'  title='Parameter menu (Boost+Settings 5s, Standby)'>&#128295;</button>"
            "</div>"
          "</div>"
          "<div id='temp' class='sect hidden'>"
            "<div class='row row4'>"
              "<button onclick='tstep(-1)' title='-1&deg;'>&#8722;</button>"
              "<span id='tval'>--&deg;</span>"
              "<button onclick='tstep(1)' title='+1&deg;'>&#43;</button>"
            "</div>"
            "<div class='row row5'>"
              "<button onclick='tset(50)'>50&deg;</button>"
              "<button onclick='tset(55)'>55&deg;</button>"
              "<button onclick='tset(60)'>60&deg;</button>"
              "<button id='setbtn' onclick='tapply()'>Set</button>"
            "</div>"
          "</div>"
        "</div>"
        "<span id='status'></span>"
      "</div>"
      "<script>"
      "document.getElementById('stream').src='http://'+location.hostname+':81';"
      "function press(btn){"
        "var st=document.getElementById('status');"
        "st.textContent='...';"
        "fetch('/api/touch',{method:'POST',"
          "headers:{'Content-Type':'application/json'},"
          "body:JSON.stringify({button:btn})})"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
          "st.textContent=d.ok?'OK':'Err';"
          "setTimeout(function(){st.textContent='';},1500);"
        "}).catch(function(){st.textContent='Err';});"
      "}"
      "function shortcut(name){"
        "var st=document.getElementById('status');"
        "st.textContent='...';"
        "fetch('/api/touch',{method:'POST',"
          "headers:{'Content-Type':'application/json'},"
          "body:JSON.stringify({shortcut:name})})"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
          "st.textContent=d.ok?'OK':'Err';"
          "setTimeout(function(){st.textContent='';},1500);"
        "}).catch(function(){st.textContent='Err';});"
      "}"
      "var tgt=55;"
      "function tshow(){document.getElementById('tval').textContent=tgt+'\\u00b0';}"
      "function tstep(d){tgt+=d;if(tgt<10)tgt=10;if(tgt>80)tgt=80;tshow();}"
      "function tset(v){tgt=v;tshow();}"
      "function tload(){"
        "fetch('/api/soll').then(function(r){return r.json();})"
        ".then(function(d){if(d.soll>0){tgt=d.soll;tshow();}})"
        ".catch(function(){});"
      "}"
      "function tapply(){"
        "var st=document.getElementById('status');"
        "var goal=tgt;"
        "st.className='busy';"
        "st.textContent='\\u23f3 bitte warten\\u2026';"
        // 30s Timeout: der Closed-Loop kann bei großen Sprüngen lange dauern,
        // daher dem fetch ausdrücklich mehr Zeit geben als den Browser-Default.
        "var ctl=new AbortController();"
        "var tmr=setTimeout(function(){ctl.abort();},30000);"
        "fetch('/api/touch',{method:'POST',signal:ctl.signal,"
          "headers:{'Content-Type':'application/json'},"
          "body:JSON.stringify({set_temp:goal})})"
        ".then(function(r){return r.json();})"
        ".then(function(d){"
          "clearTimeout(tmr);st.className='';"
          "if(d.ok){st.textContent='\\u2713 '+d.value+'\\u00b0';"
            "if(d.value>0){tgt=d.value;tshow();}}"
          "else{st.textContent='\\u2715 '+(d.error||'Fehler');}"
          "setTimeout(function(){st.textContent='';st.className='';},3000);"
        "}).catch(function(){"
          "clearTimeout(tmr);st.className='';"
          "st.textContent='\\u2715 Timeout';"
          "setTimeout(function(){st.textContent='';st.className='';},3000);"
        "});"
      "}"
      "function toggle(id){"
        "var sec=document.getElementById(id);"
        "var btn=document.getElementById(id=='scut'?'tgScut':'tgTemp');"
        "var show=sec.classList.contains('hidden');"
        "sec.classList.toggle('hidden',!show);"
        "btn.classList.toggle('open',show);"
        "if(id=='temp'&&show)tload();"   // beim Aufklappen aktuellen Soll holen
      "}"
      "</script></body></html>";

    server.send(200, "text/html", html);
  });

  // GET /api/soll – aktueller Soll-Wert (entprellt) für die Live-Popup-Anzeige.
  // Liefert {"soll":55} oder {"soll":-1} wenn nicht lesbar (Standby/gesperrt).
  server.on("/api/soll", HTTP_GET, [&]() {
    int soll = readCurrentSoll();
    server.send(200, "application/json", String("{\"soll\":") + soll + "}");
  });

  server.on("/api/status", HTTP_GET, [&]() {
    JsonDocument doc;

    doc["wifi"]["connected"] = WiFi.status() == WL_CONNECTED;
    doc["wifi"]["ip"] = (WiFi.status() == WL_CONNECTED)
                            ? WiFi.localIP().toString()
                            : WiFi.softAPIP().toString();
    doc["wifi"]["mode"] = wifiCurrentModeString();
    doc["wifi"]["ap_ssid"] = wifiApSsid();
    doc["wifi"]["ssid"] = cfg.wifi.ssid;
    doc["wifi"]["rssi"] = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;

    doc["mqtt"]["connected"] = mqtt.connected();
    doc["mqtt"]["image_enabled"] = cfg.mqtt.image_enabled;

    doc["camera"]["width"] = cfg.camera.width;
    doc["camera"]["height"] = cfg.camera.height;
    doc["camera"]["sensor"] = cameraSensorName();
    doc["camera"]["frame"] = String(cfg.camera.width) + "x" + String(cfg.camera.height);
    doc["camera"]["jpeg_quality"] = cfg.camera.jpeg_quality;
    doc["camera"]["auto_exposure"] = cfg.camera.auto_exposure;
    doc["camera"]["auto_gain"] = cfg.camera.auto_gain;
    doc["camera"]["auto_whitebalance"] = cfg.camera.auto_whitebalance;
    doc["camera"]["aec_value"] = cfg.camera.aec_value;
    doc["camera"]["agc_gain"] = cfg.camera.agc_gain;
    doc["camera"]["ok"] = cameraIsHealthy();
    doc["camera"]["capture_failures"] = cameraCaptureFailureCount();

    doc["stream"]["enabled"] = cfg.stream.enabled;

    doc["room"]["enabled"] = cfg.room_sensor.enabled;
    doc["room"]["available"] = RoomSensorSht31.isAvailable();
    doc["room"]["temp_c"] = RoomSensorSht31.lastTempC();
    doc["room"]["humidity_pct"] = RoomSensorSht31.lastHumidityPct();

#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif
    doc["system"]["fw_version"] = FW_VERSION;
    doc["system"]["heap_free"] = ESP.getFreeHeap();
    doc["system"]["uptime_s"] = millis() / 1000UL;
    doc["system"]["temperature"] = (int)temperatureRead();

    doc["ota"]["running"] = g_updateRunning;
    doc["ota"]["total"] = g_updateTotal;
    doc["ota"]["written"] = g_updateWritten;
    doc["ota"]["ok"] = g_updateOk;
    doc["ota"]["error"] = g_updateError;

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  server.on("/api/config", HTTP_GET, [&]() {
    LOGD("HTTP GET /api/config\n");
    server.send(200, "application/json", configToJson(cfg));
  });

  server.on("/api/config", HTTP_POST, [&]() {
    LOGD("HTTP POST /api/config\n");

    String body = server.arg("plain");
    int oldCameraWidth = cfg.camera.width;
    int oldCameraHeight = cfg.camera.height;

    if (!configFromJson(body, cfg)) {
      LOGE("Invalid JSON in /api/config\n");
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
      return;
    }

    if (!saveConfigToFile(cfg)) {
      LOGE("Failed to save config to NVS\n");
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    LOGI("Config updated via web\n");

    bool camModelChanged = false; // Modell fest verdrahtet
    bool camFrameChanged = (oldCameraWidth != cfg.camera.width) ||
                           (oldCameraHeight != cfg.camera.height);

    if (camModelChanged) {
      LOGI("Restarting device (cam_model=%d)\n", camModelChanged);
      server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
      delay(500);
      ESP.restart();
      return;
    }

    if (camFrameChanged) {
      LOGI("Camera frame changed: %dx%d -> %dx%d, restarting camera\n",
           oldCameraWidth,
           oldCameraHeight,
           cfg.camera.width,
           cfg.camera.height);

      if (!cameraRestart(cfg)) {
        LOGE("Camera restart failed after frame change\n");
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"camera restart failed\"}");
        return;
      }
    }

    cameraApplySettings(cfg);

    // Antwort zuerst senden, dann MQTT — Discovery kann mehrere Sekunden dauern
    // (50 ms delay pro ROI + 500 ms Retry) und würde sonst den HTTP-Client zum
    // Timeout bringen (Failed to fetch).
    server.send(200, "application/json", "{\"ok\":true}");

    mqttApplyConfig(mqtt, cfg);
    mqtt.setBufferSize(mqttBufferSizeForConfig(cfg));
    mqttPublishHomeAssistantDiscovery(mqtt, cfg);
  });


  server.on("/api/wifi_scan", HTTP_GET, [&]() {
    LOGD("HTTP GET /api/wifi_scan\n");

    wifi_mode_t mode = WiFi.getMode();
    if (mode != WIFI_AP_STA && mode != WIFI_STA) {
      // Pure AP mode has no STA interface – switch to AP_STA so scanning works.
      WiFi.mode(WIFI_AP_STA);
      delay(500);
    }

    WiFi.scanDelete();
    // Async scan: the WiFi task continues running (keeps AP alive for connected clients).
    // Blocking scan would starve the AP during channel hops and drop the browser connection.
    WiFi.scanNetworks(true, true);

    unsigned long scanStart = millis();
    int n = WIFI_SCAN_RUNNING;
    while (n == WIFI_SCAN_RUNNING && millis() - scanStart < 10000UL) {
      delay(100);
      n = WiFi.scanComplete();
    }

    if (n < 0) {
      LOGE("WiFi scan failed or timed out (result=%d)\n", n);
      WiFi.scanDelete();
      server.send(200, "application/json", "{\"ok\":false,\"count\":0,\"networks\":[]}");
      return;
    }

    JsonDocument doc;
    doc["ok"] = true;
    doc["count"] = 0;
    JsonArray nets = doc["networks"].to<JsonArray>();

    for (int i = 0; i < n; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() == 0) continue;

      JsonObject item = nets.add<JsonObject>();
      item["ssid"] = ssid;
      item["rssi"] = WiFi.RSSI(i);
      item["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
    }
    doc["count"] = nets.size();

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);

    WiFi.scanDelete();
  });

  server.on("/api/wifi_setup", HTTP_POST, [&]() {
    LOGD("HTTP POST /api/wifi_setup\n");

    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      LOGE("Invalid JSON in /api/wifi_setup\n");
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
      return;
    }

    const String ssid = jsonStringOrDefault(doc["ssid"], "");
    const String password = jsonStringOrDefault(doc["password"], "");
    const String hostname = jsonStringOrDefault(doc["hostname"], "hp-remote");

    cfg.wifi.ssid = ssid;
    cfg.wifi.password = password;
    cfg.wifi.hostname = hostname;

    if (!saveConfigToFile(cfg)) {
      LOGE("Failed to save WiFi config\n");
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }

    LOGI("WiFi setup saved, trying STA connect while AP stays active\n");

    // Keep the setup AP alive so the browser can still receive the result page,
    // while we try to join the target WLAN in parallel.
    WiFi.mode(WIFI_AP_STA);

    if (hostname.length() > 0) {
      WiFi.setHostname(hostname.c_str());
    }

    WiFi.begin(ssid.c_str(), password.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 20000UL) {
      delay(250);
    }

    if (WiFi.status() != WL_CONNECTED) {
      LOGE("WiFi setup test connect failed, staying in AP mode\n");
      WiFi.disconnect(true, false);
      WiFi.mode(WIFI_AP);
      server.send(200, "application/json",
                  "{\"ok\":false,\"connected\":false,\"error\":\"wifi connect failed\"}");
      return;
    }

    const String ip = WiFi.localIP().toString();
    LOGI("WiFi setup successful, STA IP: %s\n", ip.c_str());

    String resp = String("{\"ok\":true,\"connected\":true,\"ip\":\"") + ip +
                  "\",\"ssid\":\"" + ssid + "\",\"hostname\":\"" + hostname + "\"}";
    server.send(200, "application/json", resp);

    // Give the browser time to display the assigned IP before leaving AP mode.
    delay(12000);
    ESP.restart();
  });

  server.on("/api/snapshot", HTTP_GET, [&]() {
    LOGD("HTTP GET /api/snapshot\n");

    camera_fb_t *fb = cameraCapture();
    if (!fb) {
      server.send(500, "text/plain", "capture failed");
      return;
    }

    uint8_t *jpg_buf = nullptr;
    size_t jpg_len = 0;
    bool ok = cameraToJpeg(fb, cfg.camera.jpeg_quality, &jpg_buf, &jpg_len);
    cameraRelease(fb);

    if (!ok || !jpg_buf) {
      server.send(500, "text/plain", "jpeg conversion failed");
      return;
    }

    server.sendHeader("Cache-Control", "no-store");
    server.setContentLength(jpg_len);
    server.send(200, "image/jpeg", "");
    server.client().write(jpg_buf, jpg_len);
    free(jpg_buf);
  });

  server.on("/api/preview", HTTP_GET, [&]() {
    LOGD("HTTP GET /api/preview\n");

    if (!cfg.stream.enabled) {
      server.send(403, "text/plain", "stream disabled");
      return;
    }

    bool withLight = server.hasArg("light") && server.arg("light") == "1";

    camera_fb_t *fb = cameraCapture();
    if (!fb) {
      server.send(503, "text/plain", "capture failed");
      return;
    }

    uint8_t *jpg_buf = nullptr;
    size_t jpg_len = 0;
    bool ok = cameraToJpeg(fb, cfg.camera.jpeg_quality, &jpg_buf, &jpg_len);
    cameraRelease(fb);

    if (!ok || !jpg_buf) {
      if (jpg_buf) free(jpg_buf);
      server.send(503, "text/plain", "jpeg conversion failed");
      return;
    }

    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.setContentLength(jpg_len);
    server.send(200, "image/jpeg", "");
    server.client().write(jpg_buf, jpg_len);
    free(jpg_buf);
  });

  server.on("/api/evaluate", HTTP_POST, [&]() {
    LOGD("HTTP POST /api/evaluate\n");

    String body = server.arg("plain");
    if (body.length() > 2) {
      configFromJson(body, cfg);
      cameraApplySettings(cfg);
      }

    camera_fb_t *fb = cameraCapture();
    if (!fb) {
      LOGE("capture failed in /api/evaluate\n");
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"capture failed\"}");
      return;
    }

    String out = evaluateCurrent(fb, cfg);
    cameraRelease(fb);

    lastPayload = out;
    server.send(200, "application/json", out);
  });


  // ── HP-Remote Touch API ───────────────────────────────────────────────────
  // POST /api/touch  – verschiedene Modi:
  //   {"button":"up"}                       einzelner Druck
  //   {"button":"up","count":5}             5x drücken
  //   {"button":"settings","duration":5000} langer Druck (5s)
  //   {"combo":["up","down"],"duration":3000} zwei Tasten gleichzeitig (3s)
  //   {"sequence":["settings","up"]}        nacheinander
  server.on("/api/touch", HTTP_POST, [&]() {
    LOGD("HTTP POST /api/touch\n");
    String body = server.arg("plain");
    if (body.length() < 3) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
      return;
    }

    // Direkt an den Touch-Callback weiterleiten (gleiche Logik wie MQTT).
    // Das topic-Argument wird im Callback nicht ausgewertet, nur der Payload.
    mqttTouchCallback("", (byte*)body.c_str(), body.length());

    // Echtes Ergebnis zurückgeben (bei set_temp der erreichte Wert / Fehler).
    server.send(200, "application/json", g_lastTouchResult);
  });

  // POST /api/i2c/scan – kompletten I2C-Bus neu scannen (nach Anschließen von
  // Touch-Platine/Raumsensor). pcf8574_found bleibt für Abwärtskompatibilität
  // erhalten, devices[] listet alle gefundenen Adressen inkl. bekannter Namen.
  server.on("/api/i2c/scan", HTTP_POST, [&]() {
    LOGI("HTTP POST /api/i2c/scan\n");
    bool pcfFound = WpTouch.rescan();

    JsonDocument doc;
    doc["ok"] = true;
    doc["pcf8574_found"] = pcfFound;

    JsonArray devices = doc["devices"].to<JsonArray>();
    for (uint8_t addr : WpTouch.scanBus()) {
      JsonObject dev = devices.add<JsonObject>();
      char addrStr[6];
      snprintf(addrStr, sizeof(addrStr), "0x%02X", addr);
      dev["address"] = addrStr;
      if (addr == PCF8574_ADDR) dev["name"] = "PCF8574 (Touch)";
      else if (addr == SHT31_ADDR) dev["name"] = "SHT31 (Room)";
    }

    String resp;
    serializeJson(doc, resp);
    server.send(200, "application/json", resp);
  });

  // POST /api/touch/hold  {"button":"power"} – Port dauerhaft LOW (Multimeter-Test)
  server.on("/api/touch/hold", HTTP_POST, [&]() {
    String body = server.arg("plain");
    JsonDocument doc;
    if (deserializeJson(doc, body) != DeserializationError::Ok ||
        !doc["button"].is<const char*>()) {
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"need button\"}");
      return;
    }
    bool ok = WpTouch.hold(doc["button"].as<String>());
    server.send(200, "application/json",
                String("{\"ok\":") + (ok ? "true" : "false") + "}");
  });

  // POST /api/touch/release – alle Ports HIGH (loslassen)
  server.on("/api/touch/release", HTTP_POST, [&]() {
    WpTouch.releaseAll();
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/reboot", HTTP_POST, [&]() {
    LOGI("HTTP POST /api/reboot -> restarting device\n");
    server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
    delay(500);
    ESP.restart();
  });

  // POST /api/wifi_reset – WLAN-Zugangsdaten löschen und neu starten.
  // Beim Neustart scheitert die (leere) WLAN-Verbindung → Setup-AP startet.
  server.on("/api/wifi_reset", HTTP_POST, [&]() {
    LOGI("HTTP POST /api/wifi_reset -> clearing WiFi, restarting into AP\n");
    cfg.wifi.ssid = "";
    cfg.wifi.password = "";
    saveConfigToFile(cfg);
    server.send(200, "application/json",
                "{\"ok\":true,\"info\":\"WiFi cleared, restarting into setup AP\"}");
    delay(500);
    ESP.restart();
  });

  server.on(
    "/api/update",
    HTTP_POST,
    [&]() {
      bool ok = !Update.hasError() &&
                Update.isFinished() &&
                g_updateError.length() == 0;

      const esp_partition_t *running = esp_ota_get_running_partition();
      const esp_partition_t *boot = esp_ota_get_boot_partition();
      const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);

      if (running) {
        LOGI("OTA POST: running partition label=%s subtype=%d address=0x%08x size=%u\n",
             running->label,
             running->subtype,
             (unsigned)running->address,
             (unsigned)running->size);
      }

      if (boot) {
        LOGI("OTA POST: boot partition label=%s subtype=%d address=0x%08x size=%u\n",
             boot->label,
             boot->subtype,
             (unsigned)boot->address,
             (unsigned)boot->size);
      }

      if (next) {
        LOGI("OTA POST: next update partition label=%s subtype=%d address=0x%08x size=%u\n",
             next->label,
             next->subtype,
             (unsigned)next->address,
             (unsigned)next->size);
      }

      if (ok) {
        g_updateOk = true;
        g_updateRunning = false;
        g_updateError = "";

        LOGI("Browser OTA finished successfully\n");

        server.send(200, "application/json", "{\"ok\":true,\"restart\":true}");
        delay(1000);
        ESP.restart();
      } else {
        g_updateOk = false;
        g_updateRunning = false;

        if (g_updateError.length() == 0) {
          g_updateError = Update.errorString();
        }

        LOGE("Browser OTA failed: %s\n", g_updateError.c_str());
        server.send(500, "application/json", "{\"ok\":false,\"error\":\"update failed\"}");
      }
    },
    [&]() {
      HTTPUpload &upload = server.upload();

      if (wifiCurrentModeString() != "STA") {
        LOGE("Browser OTA refused in AP mode\n");
        return;
      }

      if (upload.status == UPLOAD_FILE_START) {
        LOGI("Browser OTA upload start: %s size=%u\n", upload.filename.c_str(), (unsigned)upload.totalSize);

        const esp_partition_t *running = esp_ota_get_running_partition();
        const esp_partition_t *boot = esp_ota_get_boot_partition();
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        g_updateTargetPartition = next;

        if (running) {
          LOGI("OTA START: running partition label=%s subtype=%d address=0x%08x size=%u\n",
               running->label,
               running->subtype,
               (unsigned)running->address,
               (unsigned)running->size);
        }

        if (boot) {
          LOGI("OTA START: boot partition label=%s subtype=%d address=0x%08x size=%u\n",
               boot->label,
               boot->subtype,
               (unsigned)boot->address,
               (unsigned)boot->size);
        }

        if (next) {
          LOGI("OTA START: next update partition label=%s subtype=%d address=0x%08x size=%u\n",
               next->label,
               next->subtype,
               (unsigned)next->address,
               (unsigned)next->size);
        } else {
          LOGE("OTA START: no next update partition found\n");
        }

        g_updateRunning = true;
        g_updateOk = false;
        g_updateError = "";
        g_updateWritten = 0;
        g_updateTotal = upload.totalSize;

        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          g_updateError = Update.errorString();
          LOGE("Update.begin failed: %s\n", g_updateError.c_str());
        }
      } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (g_updateWritten == 0 && upload.currentSize > 0) {
          LOGI("OTA first chunk: 0x%02X 0x%02X 0x%02X 0x%02X\n",
               upload.buf[0], upload.buf[1], upload.buf[2], upload.buf[3]);
        }
        size_t written = Update.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          g_updateError = Update.errorString();
          LOGE("Update.write failed: %s\n", g_updateError.c_str());
        } else {
          g_updateWritten += upload.currentSize;
        }
      } else if (upload.status == UPLOAD_FILE_END) {
        if (!Update.end(true)) {
          g_updateError = Update.errorString();
          LOGE("Update.end failed: %s\n", g_updateError.c_str());
        } else {
          LOGI("Browser OTA upload complete: %u bytes\n", (unsigned)g_updateWritten);

          if (!g_updateTargetPartition) {
            g_updateError = "no target partition";
            LOGE("OTA END: target partition missing\n");
          } else {
            esp_err_t err = esp_ota_set_boot_partition(g_updateTargetPartition);
            if (err != ESP_OK) {
              g_updateError = String("esp_ota_set_boot_partition failed: ") + String((int)err);
              LOGE("OTA END: esp_ota_set_boot_partition failed: %d\n", (int)err);
            } else {
              const esp_partition_t *bootNow = esp_ota_get_boot_partition();
              if (bootNow) {
                LOGI("OTA END: boot partition now label=%s subtype=%d address=0x%08x size=%u\n",
                     bootNow->label,
                     bootNow->subtype,
                     (unsigned)bootNow->address,
                     (unsigned)bootNow->size);
              }

              if (!bootNow || strcmp(bootNow->label, g_updateTargetPartition->label) != 0) {
                g_updateError = "boot partition did not switch";
                LOGE("OTA END: boot partition did not switch to target\n");
              }
            }
          }
        }
      } else if (upload.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        g_updateRunning = false;
        g_updateOk = false;
        g_updateError = "aborted";
        LOGE("Browser OTA aborted\n");
      }
    }
  );

  server.on("/api/config/export", HTTP_GET, [&]() {
    LOGD("HTTP GET /api/config/export\n");
    
    String json = configToJson(cfg);
    
    server.sendHeader("Content-Disposition", "attachment; filename=\"HP-Remote-config.json\"");
    server.send(200, "application/json", json);
  });

  server.on("/api/config/import", HTTP_POST, [&]() {
    LOGD("HTTP POST /api/config/import\n");
    
    String body = server.arg("plain");
    
    if (body.length() == 0) {
      LOGE("Import: empty body\n");
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"empty body\"}");
      return;
    }
    
    AppConfig newCfg = cfg;
    if (!configFromJson(body, newCfg)) {
      LOGE("Import: invalid JSON or parse error\n");
      server.send(400, "application/json", "{\"ok\":false,\"error\":\"invalid json\"}");
      return;
    }
    
    cfg = newCfg;
    
    if (!saveConfigToFile(cfg)) {
      LOGE("Import: failed to save config\n");
      server.send(500, "application/json", "{\"ok\":false,\"error\":\"save failed\"}");
      return;
    }
    
    LOGI("Config imported successfully\n");
    server.send(200, "application/json", "{\"ok\":true,\"message\":\"Config imported. Please restart the device.\"}");
  });

  server.begin();
}
