// zones.h — FROZEN PUBLIC CONTRACT (WP1)
//
// Relay control + scheduling engine + rain sensor, ported 1:1 from src/main.cpp
// (setZone/allZonesOff/checkRainSensor/checkSchedules and the per-zone state).
// Siblings (WP3 web_server, WP4 mqtt_client) drive zones and read state ONLY
// through this interface — no reaching into internals.

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Configure relay GPIOs (active-LOW, all OFF) and the rain sensor input.
// Call once at startup before any zone_* call.
void zones_gpio_init(void);

// Drive a relay. z >= ZONE_COUNT is ignored. Turning ON records the start
// timestamp; turning OFF clears it. Mirrors setZone().
void zone_set(uint8_t z, bool on);

// Turn every zone off. Mirrors allZonesOff().
void all_zones_off(void);

// Read the rain sensor; on a raining edge with config.rainOverride, force all
// zones off. Updates the internal rain flag. Mirrors checkRainSensor().
void check_rain_sensor(void);

// Schedule engine tick. Internally gated on g_time_synced and rain override.
// Starts at most one zone at a time (time-of-day + day-of-week match) and
// auto-stops a running zone once its duration elapses. Mirrors checkSchedules().
void check_schedules(void);

// Convenience for the `?action=run&seconds=N` path: set the zone's duration to
// `seconds` then start it (matches handleZoneCommand's "run" branch).
void zone_run_for(uint8_t z, uint16_t seconds);

// ─── Getters for web / mqtt / sse ───────────────────────────────────────────
// Is the zone's relay currently energized?
bool zone_is_active(uint8_t z);

// Seconds remaining for an active zone: duration - elapsed. RAW value to match
// the original /api/zones output exactly (it is NOT clamped and may briefly go
// slightly negative between schedule ticks). Returns 0 for an inactive zone.
// Callers that need a clamped value (e.g. the SSE payload) must clamp to >= 0.
int zone_remaining(uint8_t z);

// Current debounced rain state. Mirrors the `rainDetected` global.
bool rain_detected(void);

#ifdef __cplusplus
}
#endif
