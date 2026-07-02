#include "camera_service.h"
#include "img_converters.h"
#include "debug_log.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static bool g_cameraHealthy = false;
static unsigned long g_captureFailureCount = 0;
static AppConfig g_lastCfg;

static framesize_t cameraFrameSizeFromConfig(const AppConfig &cfg, int &resolvedW, int &resolvedH) {
  if (cfg.camera.width == 160 && cfg.camera.height == 120) {
    resolvedW = 160;
    resolvedH = 120;
    return FRAMESIZE_QQVGA;
  }

  if (cfg.camera.width == 320 && cfg.camera.height == 240) {
    resolvedW = 320;
    resolvedH = 240;
    return FRAMESIZE_QVGA;
  }

  if (cfg.camera.width == 640 && cfg.camera.height == 480) {
    resolvedW = 640;
    resolvedH = 480;
    return FRAMESIZE_VGA;
  }

  resolvedW = 800;
  resolvedH = 600;
  return FRAMESIZE_SVGA;
}

// ── ESP32-S3-CAM (OV2640 onboard) – fest verdrahtet ──────────────────────
// Pinout aus Boardbeschriftung (Bild):
//   SIOD=4, SIOC=5, VSYNC=6, HREF=7, XCLK=15
//   Y9=16, Y8=17, Y7=18, Y4=8, Y3=9, Y5=10, Y2=11, Y6=12, PCLK=13
// Freie GPIOs für Touch-Controller: 1, 2, 14, 21, 47, 48
static void fillCameraPins(camera_config_t &c) {
  c.pin_pwdn     = -1;
  c.pin_reset    = -1;
  c.pin_xclk     = 15;
  c.pin_sccb_sda = 4;
  c.pin_sccb_scl = 5;

  c.pin_d7 = 16;   // Y9  (MSB)
  c.pin_d6 = 17;   // Y8
  c.pin_d5 = 18;   // Y7
  c.pin_d4 = 8;    // Y4
  c.pin_d3 = 9;    // Y3
  c.pin_d2 = 10;   // Y5
  c.pin_d1 = 11;   // Y2
  c.pin_d0 = 12;   // Y6  (LSB)

  c.pin_vsync = 6;
  c.pin_href  = 7;
  c.pin_pclk  = 13;

  // ESP32-S3: LEDC für XCLK-Erzeugung
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer   = LEDC_TIMER_0;
}

static void fillCameraConfig(camera_config_t &c, const AppConfig &cfg) {
  memset(&c, 0, sizeof(c));

  fillCameraPins(c);

  c.pixel_format = PIXFORMAT_GRAYSCALE;
  int resolvedW  = 320;
  int resolvedH  = 240;
  c.frame_size   = cameraFrameSizeFromConfig(cfg, resolvedW, resolvedH);
  c.xclk_freq_hz = 20000000;   // OV2640 auf ESP32-S3: 20 MHz stabil
  c.jpeg_quality = cfg.camera.jpeg_quality;
  c.fb_count     = CAMERA_FB_COUNT;
  c.grab_mode    = CAMERA_GRAB_WHEN_EMPTY;
  c.fb_location  = CAMERA_FB_IN_PSRAM;

  LOGD("Camera frame request=%dx%d resolved=%dx%d\n",
       cfg.camera.width,
       cfg.camera.height,
       resolvedW,
       resolvedH);
}

bool cameraApplySettings(const AppConfig &cfg) {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) {
    LOGE("cameraApplySettings: sensor not available\n");
    return false;
  }

  s->set_exposure_ctrl(s, cfg.camera.auto_exposure ? 1 : 0);
  s->set_gain_ctrl(s, cfg.camera.auto_gain ? 1 : 0);
  s->set_whitebal(s, cfg.camera.auto_whitebalance ? 1 : 0);

  s->set_vflip(s, cfg.camera.vflip ? 1 : 0);
  s->set_hmirror(s, cfg.camera.hflip ? 1 : 0);

  int aec = cfg.camera.aec_value;
  if (aec < 0) aec = 0;
  if (aec > 1200) aec = 1200;

  int agc = cfg.camera.agc_gain;
  if (agc < 0) agc = 0;
  if (agc > 30) agc = 30;

  s->set_aec_value(s, aec);
  s->set_agc_gain(s, agc);

  LOGI("Camera tuning applied\n");
  LOGD("Camera tuning: auto_exp=%d auto_gain=%d auto_wb=%d aec=%d agc=%d\n",
       cfg.camera.auto_exposure,
       cfg.camera.auto_gain,
       cfg.camera.auto_whitebalance,
       aec,
       agc);

  return true;
}


bool cameraStart(const AppConfig &cfg) {
  g_lastCfg = cfg;

  camera_config_t c;
  fillCameraConfig(c, cfg);

  LOGI("Initializing camera: ESP32-S3-CAM OV2640\n");
  LOGD("Camera init params: xclk=%u frame=%dx%d jpeg_quality=%d\n",
       c.xclk_freq_hz,
       cfg.camera.width,
       cfg.camera.height,
       cfg.camera.jpeg_quality);

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    LOGE("esp_camera_init failed: 0x%x, retrying...\n", err);
    esp_camera_deinit();
    delay(300);
    err = esp_camera_init(&c);
    if (err != ESP_OK) {
      LOGE("esp_camera_init retry failed: 0x%x\n", err);
      g_cameraHealthy = false;
      return false;
    }
    LOGI("Camera init succeeded on retry\n");
  }

  g_cameraHealthy = true;
  g_captureFailureCount = 0;

  cameraApplySettings(cfg);

  // OV2640 AEC/AGC braucht mehrere Frames zum Einschwingen.
  // Warm-up-Frames verwerfen, damit der erste echte Capture korrekte Helligkeitswerte liefert.
  // Ohne Warm-up: alle Segment-Mittelwerte nahe 0 → bitsToDigit() → -1 → payload invalid.
  for (int i = 0; i < 5; i++) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
    delay(100);
  }

  LOGI("Camera init OK\n");
  LOGD("Camera settings: frame=%dx%d jpeg_quality=%d\n",
       cfg.camera.width,
       cfg.camera.height,
       cfg.camera.jpeg_quality);




  return true;
}

bool cameraRestart(const AppConfig &cfg) {
  LOGI("Restarting camera\n");

  // Failure-Counter zurücksetzen, damit der Watchdog nicht sofort wieder auslöst.
  g_captureFailureCount = 0;

  // Licht-Pin vor dem Kamera-Restart freigeben (AI-Thinker GPIO4 Flash-LED).

  esp_camera_deinit();
  delay(300);

  bool ok = cameraStart(cfg);

  // Licht immer wiederherstellen – auch wenn Camera-Restart fehlschlug.
  // Sonst bleibt GPIO4 dauerhaft als INPUT und die LED lässt sich nicht mehr schalten.
  return ok;
}

camera_fb_t* cameraCapture() {
  // Bei GRAB_WHEN_EMPTY füllen sich die fb_count Buffer bei Inaktivität
  // mit veralteten Frames. Alle Buffer leeren bevor wir den echten Frame holen.
  for (int i = 0; i < CAMERA_FB_COUNT; i++) {
    camera_fb_t *dummy = esp_camera_fb_get();
    if (dummy) esp_camera_fb_return(dummy);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    vTaskDelay(pdMS_TO_TICKS(25));
    fb = esp_camera_fb_get();
  }
  if (!fb) {
    LOGE("esp_camera_fb_get() failed\n");
    g_cameraHealthy = false;
    g_captureFailureCount++;
  } else {
    g_cameraHealthy = true;
  }
  return fb;
}

// Schnelle Capture-Variante für den Live-Stream: KEIN Dummy-Frame-Dropping.
// Liefert direkt den nächsten verfügbaren Frame → ~3x schneller als cameraCapture().
// Ein minimal älterer Frame ist beim flüssigen Video unkritisch.
camera_fb_t* cameraCaptureFast() {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    g_cameraHealthy = false;
    g_captureFailureCount++;
  } else {
    g_cameraHealthy = true;
  }
  return fb;
}

void cameraRelease(camera_fb_t *fb) {
  if (fb) esp_camera_fb_return(fb);
}

bool cameraToJpeg(camera_fb_t *fb, int quality, uint8_t **jpg_buf, size_t *jpg_len) {
  bool ok = frame2jpg(fb, quality, jpg_buf, jpg_len);
  if (!ok) {
    LOGE("frame2jpg() failed\n");
  } else {
    LOGD("frame2jpg() ok, len=%u\n", (unsigned)*jpg_len);
  }
  return ok;
}

// Downscale grayscale image by 2x (average 2x2 blocks) then encode as JPEG.
bool cameraToJpegScaled(camera_fb_t *fb, int quality, uint8_t **jpg_buf, size_t *jpg_len) {
  if (!fb || !fb->buf || fb->len == 0) return false;

  uint16_t sw = fb->width / 2;
  uint16_t sh = fb->height / 2;
  size_t sbytes = (size_t)sw * sh;

  uint8_t *sbuf = (uint8_t *)malloc(sbytes);
  if (!sbuf) {
    LOGE("cameraToJpegScaled: malloc failed\n");
    return false;
  }

  const uint8_t *src = fb->buf;
  uint16_t stride = fb->width;
  for (uint16_t y = 0; y < sh; y++) {
    for (uint16_t x = 0; x < sw; x++) {
      uint16_t r0 = y * 2, c0 = x * 2;
      uint16_t avg = (uint16_t)src[r0 * stride + c0]
                   + src[r0 * stride + c0 + 1]
                   + src[(r0+1) * stride + c0]
                   + src[(r0+1) * stride + c0 + 1];
      sbuf[y * sw + x] = (uint8_t)(avg >> 2);
    }
  }

  bool ok = fmt2jpg(sbuf, sbytes, sw, sh, PIXFORMAT_GRAYSCALE, quality, jpg_buf, jpg_len);
  free(sbuf);

  if (!ok) {
    LOGE("cameraToJpegScaled: fmt2jpg failed\n");
  } else {
    LOGD("cameraToJpegScaled: ok, %ux%u -> len=%u\n", sw, sh, (unsigned)*jpg_len);
  }
  return ok;
}

bool cameraIsHealthy() {
  return g_cameraHealthy;
}

void cameraMarkCaptureFailure() {
  g_cameraHealthy = false;
  g_captureFailureCount++;
}

void cameraMarkCaptureSuccess() {
  g_cameraHealthy = true;
}

unsigned long cameraCaptureFailureCount() {
  return g_captureFailureCount;
}

const char* cameraSensorName() {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return "unknown";

  switch (s->id.PID) {
    case OV2640_PID:
      return "OV2640";
    case OV3660_PID:
      return "OV3660";
    case OV5640_PID:
      return "OV5640";
    default:
      return "unknown";
  }
}
