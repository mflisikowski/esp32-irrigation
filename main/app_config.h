// app_config.h — FROZEN PUBLIC CONTRACT (WP1)
//
// Port of the Config/ZoneConfig structs and DEFAULT_CONFIG from the original
// Arduino firmware (src/main.cpp). Field order and types are kept identical so
// the NVS blob layout matches the documented behavior. Persisted as a raw blob
// via NVS under namespace "irrig", key "config" (mirrors loadConfig/saveConfig).
//
// Shared by all sibling packages (WiFi/time, HTTP server, MQTT). Read/write the
// live configuration through `g_config`; the time-synced flag lives here too so
// the schedule engine and the web/info endpoint can read it without depending
// on the time_sync module's header.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ─── Shared constants ───────────────────────────────────────────────────────
#define ZONE_COUNT 6
#define FIRMWARE_VERSION "1.0.0"

// NVS storage location (kept identical to the Arduino Preferences usage).
#define CONFIG_NVS_NAMESPACE "irrig"
#define CONFIG_NVS_KEY       "config"

// ─── Config structs (byte-for-byte layout from src/main.cpp) ────────────────
typedef struct {
    char     name[24];
    bool     enabled;
    uint8_t  days;        // bitmask: 0=Sun,1=Mon...6=Sat
    uint8_t  startHour;
    uint8_t  startMinute;
    uint16_t duration;    // seconds
} zone_config_t;

typedef struct {
    zone_config_t zones[ZONE_COUNT];
    bool     rainOverride;
    uint8_t  mqttEnabled;
    char     mqttServer[40];
    uint16_t mqttPort;
    char     mqttUser[32];
    char     mqttPass[32];
    char     mqttTopic[32];
    bool     flowEnabled;
    char     wifiSsid[33];
    char     wifiPass[65];
} app_config_t;

// ─── Live global state ──────────────────────────────────────────────────────
// Live configuration. Siblings read/mutate fields directly, then call
// config_save() to persist.
extern app_config_t g_config;

// Set true by the time_sync module (WP2) once SNTP has a valid epoch.
// Read by the schedule engine (zones.c) and the /api/info handler (WP3).
extern volatile bool g_time_synced;

// ─── API ────────────────────────────────────────────────────────────────────
// nvs_get_blob under (CONFIG_NVS_NAMESPACE, CONFIG_NVS_KEY). If the stored size
// != sizeof(app_config_t) (or no value), copies DEFAULT_CONFIG and saves it.
// Mirrors loadConfig().
void config_load(void);

// nvs_set_blob + commit of g_config. Mirrors saveConfig().
void config_save(void);

#ifdef __cplusplus
}
#endif
