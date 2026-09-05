#pragma once

#include <PubSubClient.h>
#include "app_config.h"

void mqttPublishHomeAssistantDiscovery(PubSubClient &mqtt, const AppConfig &cfg);
void mqttPublishTouchDiscovery(PubSubClient &mqtt, const AppConfig &cfg);
void mqttPublishStreamDiscovery(PubSubClient &mqtt, const AppConfig &cfg);

// HP-Remote: Heizraum-Sensor (SHT31, optional) – nur aufrufen, wenn der
// Sensor aktuell erkannt ist (RoomSensorSht31.isAvailable()).
void mqttPublishRoomSensorDiscovery(PubSubClient &mqtt, const AppConfig &cfg);