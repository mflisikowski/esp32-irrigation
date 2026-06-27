// mqtt_client.h — WP4: esp-mqtt client (native ESP-IDF)
//
// Ports the Arduino PubSubClient logic from src/main.cpp (mqttCallback,
// reconnectMQTT, publishMQTT, and the MQTT ticking in loop()) onto esp-mqtt.
// No Home Assistant auto-discovery — the original doesn't do it.

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Start the esp-mqtt client IF g_config.mqttEnabled and a plausible server is
// configured. Safe to call once at startup; a no-op when MQTT is disabled.
void mqtt_init(void);

// Called every ~250ms from the loop task. esp-mqtt auto-reconnects on its own,
// so this mainly self-rate-limits the periodic (~15s) status publish.
void mqtt_tick(void);

// Current broker connection state (drives /api/info's "mqtt" field).
bool mqtt_is_connected(void);

#ifdef __cplusplus
}
#endif
