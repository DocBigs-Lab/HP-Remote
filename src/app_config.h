#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

struct WifiConfig {
  String ssid;
  String password;
  String hostname;
};

struct DeviceConfig {
  String id;
  String name;
};

struct CameraConfig {
  int width;
  int height;
  int jpeg_quality;

  bool auto_exposure;
  bool auto_gain;
  bool auto_whitebalance;

  int aec_value;
  int agc_gain;

  bool vflip;
  bool hflip;

  float fine_rotation;   // Feinjustage in Grad (-5.0 bis +5.0)
};

struct MqttConfig {
  bool enabled;
  bool image_enabled;
  String host;
  uint16_t port;
  String topic;
  String username;
  String password;
};

struct HomeAssistantConfig {
  bool   enabled;
  String discovery_prefix;
};

struct StreamConfig {
  bool enabled;
};

// Optionaler SHT31-Raumsensor (Heizraum-Temperatur/Luftfeuchte). Läuft am
// gemeinsamen I2C-Bus mit dem PCF8574 (touch_config.h). Wird kein Sensor
// erkannt, bleiben MQTT-Werte und HA-Discovery automatisch aus.
struct RoomSensorConfig {
  bool enabled;
  float temp_offset;
  float humidity_offset;
  int read_interval_sec;
};

struct SegSample {
  float rx, ry, rw, rh;
};

struct SevenSegProfile {
  String name;
  SegSample segs[7]; // order: a, b, c, d, e, f, g
};

struct Roi {
  String id;
  String label;
  String type;

  int x;
  int y;
  int w;
  int h;

  int threshold;
  int digit_gap_px;
  int digits;
  int decimal_places;
  std::vector<int> gaps; // per-gap spacing; empty = uniform (digit_gap_px for all)

  int threshold_on;
  int threshold_off;

  bool last_state;
  bool invert_logic;
  bool auto_threshold;
  bool stretch_contrast;
  int confidence_margin;

  bool ha_enabled;
  String ha_name;
  String ha_unit;
  String ha_icon;
  String ha_device_class;
  String ha_state_class;

  String seg_profile; // references SevenSegProfile.name; default "standard"
  bool locked;        // true = ROI vor Änderungen im Editor geschützt
};

struct AppConfig {
  WifiConfig wifi;
  DeviceConfig device;
  CameraConfig camera;
  MqttConfig mqtt;
  HomeAssistantConfig ha;
  StreamConfig stream;
  RoomSensorConfig room_sensor;

  int capture_interval_sec;
  int debug_level;

  std::vector<Roi> rois;
  std::vector<SevenSegProfile> seg_profiles;
};

void loadDefaultConfig(AppConfig &cfg);
String jsonStringOrDefault(JsonVariantConst v, const char *def);
String configToJson(const AppConfig &cfg);
bool configFromJson(const String &json, AppConfig &cfg);
