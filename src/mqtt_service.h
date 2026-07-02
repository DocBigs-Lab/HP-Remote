#pragma once

#include <Arduino.h>
#include <PubSubClient.h>
#include "app_config.h"

void mqttApplyConfig(PubSubClient &mqtt, const AppConfig &cfg);
void mqttPublishState(PubSubClient &mqtt, const AppConfig &cfg, const String &payload);
void mqttPublishImage(PubSubClient &mqtt, const AppConfig &cfg, const uint8_t *payload, size_t len);
void mqttSetGlobalClient(PubSubClient *mqtt);

// neu
void mqttPublishHealth(PubSubClient &mqtt, const AppConfig &cfg, const String &payload);
bool mqttEnsureConnected(PubSubClient &mqtt, const AppConfig &cfg);

// ─── WP-Remote Touch-Erweiterungen ───────────────────────────────────────────
void mqttTouchCallback(const char* topic, byte* payload, unsigned int len);
void mqttRegisterTouchConfig(AppConfig* cfg);

// ─── Heizraum-Sensor (SHT31, optional) ───────────────────────────────────────
void mqttPublishRoomSensorState(PubSubClient &mqtt, const AppConfig &cfg, const String &payload);
void mqttSetRoomSensorAvailability(PubSubClient &mqtt, const AppConfig &cfg, bool available);
