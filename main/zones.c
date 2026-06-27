// zones.c — relay + scheduling engine + rain sensor, ported 1:1 from main.cpp.

#include "zones.h"
#include "app_config.h"

#include <time.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "zones";

// ─── Pinout (per target) ────────────────────────────────────────────────────
// Dual-target: pins differ between the classic ESP32 (original wiring) and the
// ESP32-C6 (intended target). zones_gpio_init still guards out-of-range pins.
#if defined(CONFIG_IDF_TARGET_ESP32C6)
// C6: avoid strapping (8,9,15), USB-JTAG (12,13), UART0 console (16,17),
// SPI flash (24-30). GPIO 32/33/34 of the original don't exist on the C6.
static const int RELAY_PINS[ZONE_COUNT] = { 0, 1, 2, 3, 10, 11 };
static const int RAIN_SENSOR_PIN = 4;
#else
// Classic ESP32 (original wiring, ported verbatim from the Arduino firmware).
static const int RELAY_PINS[ZONE_COUNT] = { 32, 33, 25, 26, 27, 14 };
static const int RAIN_SENSOR_PIN = 34;
#endif

#define RELAY_ON  0   // active-LOW
#define RELAY_OFF 1

// ─── Per-zone runtime state ─────────────────────────────────────────────────
static bool     s_zone_active[ZONE_COUNT]  = { false };
static uint32_t s_zone_started[ZONE_COUNT] = { 0 };   // millis() equivalent
static uint16_t s_run_override[ZONE_COUNT] = { 0 };   // transient "run for N s" (0 = use g_config)
static bool     s_rain_detected = false;

// Guards all zone/relay state. Mutated from the loop task (check_schedules), the
// MQTT task (command callback) and the httpd task (API handlers) — recursive so
// nested calls (e.g. all_zones_off -> zone_set, check_schedules -> zone_set) work.
static SemaphoreHandle_t s_lock;

static inline void zones_lock(void)   { if (s_lock) xSemaphoreTakeRecursive(s_lock, portMAX_DELAY); }
static inline void zones_unlock(void) { if (s_lock) xSemaphoreGiveRecursive(s_lock); }

// millis() equivalent — milliseconds since boot.
static inline uint32_t millis_now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

// Effective watering duration: the transient "run for N s" override if set,
// otherwise the persisted schedule duration. Keeps g_config untouched by run.
static inline uint16_t effective_duration(uint8_t z)
{
    return s_run_override[z] ? s_run_override[z] : g_config.zones[z].duration;
}

void zones_gpio_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateRecursiveMutex();

    uint64_t out_mask = 0;
    for (int i = 0; i < ZONE_COUNT; i++) {
        if (RELAY_PINS[i] >= 0 && RELAY_PINS[i] < GPIO_NUM_MAX) {
            out_mask |= (1ULL << RELAY_PINS[i]);
        } else {
            ESP_LOGW(TAG, "relay pin %d out of range for this target — skipped", RELAY_PINS[i]);
        }
    }

    gpio_config_t out_cfg = {
        .pin_bit_mask = out_mask,
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    if (out_mask) {
        gpio_config(&out_cfg);
    }

    // Default all relays OFF (active-LOW => drive HIGH).
    for (int i = 0; i < ZONE_COUNT; i++) {
        if (RELAY_PINS[i] >= 0 && RELAY_PINS[i] < GPIO_NUM_MAX) {
            gpio_set_level(RELAY_PINS[i], RELAY_OFF);
        }
        s_zone_active[i] = false;
        s_zone_started[i] = 0;
    }

    if (RAIN_SENSOR_PIN >= 0 && RAIN_SENSOR_PIN < GPIO_NUM_MAX) {
        gpio_config_t rain_cfg = {
            .pin_bit_mask = (1ULL << RAIN_SENSOR_PIN),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,   // INPUT_PULLUP; LOW = raining
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&rain_cfg);
    } else {
        ESP_LOGW(TAG, "rain pin %d out of range for this target — rain sensing disabled", RAIN_SENSOR_PIN);
    }
}

void zone_set(uint8_t z, bool on)
{
    if (z >= ZONE_COUNT) return;
    zones_lock();
    if (RELAY_PINS[z] >= 0 && RELAY_PINS[z] < GPIO_NUM_MAX) {
        gpio_set_level(RELAY_PINS[z], on ? RELAY_ON : RELAY_OFF);
    }
    s_zone_active[z] = on;
    s_zone_started[z] = on ? millis_now() : 0;
    if (!on) s_run_override[z] = 0;   // clear any transient run-duration on stop
    zones_unlock();
}

void all_zones_off(void)
{
    zones_lock();
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
        zone_set(i, false);
    }
    zones_unlock();
}

void zone_run_for(uint8_t z, uint16_t seconds)
{
    if (z >= ZONE_COUNT) return;
    zones_lock();
    s_run_override[z] = seconds;   // transient — does NOT overwrite saved g_config
    zone_set(z, true);
    zones_unlock();
}

void check_rain_sensor(void)
{
    bool raining = false;
    if (RAIN_SENSOR_PIN >= 0 && RAIN_SENSOR_PIN < GPIO_NUM_MAX) {
        raining = (gpio_get_level(RAIN_SENSOR_PIN) == 0);  // LOW = raining
    }
    if (raining != s_rain_detected) {
        s_rain_detected = raining;
        if (raining && g_config.rainOverride) {
            all_zones_off();
        }
    }
}

static bool is_day_match(uint8_t day_mask)
{
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return (day_mask >> tm_now.tm_wday) & 1;
}

void check_schedules(void)
{
    if (!g_time_synced) return;

    zones_lock();

    // 1) Stop any active zone whose effective duration elapsed — independent of
    //    enabled/day so a manual "run for N s" (incl. on a disabled zone) always
    //    auto-stops. Uses the transient override when set, else g_config.
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
        if (!s_zone_active[i]) continue;
        uint32_t elapsed = (millis_now() - s_zone_started[i]) / 1000;
        if (elapsed >= effective_duration(i)) {
            zone_set(i, false);
            ESP_LOGI(TAG, "  STOP strefa %d (%s)", i, g_config.zones[i].name);
        }
    }

    // 2) Rain suppresses new scheduled starts (active zones are force-stopped in
    //    check_rain_sensor when rain begins).
    if (s_rain_detected && g_config.rainOverride) {
        zones_unlock();
        return;
    }

    // 3) Start a scheduled zone — only one zone runs at a time.
    bool any_active = false;
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
        if (s_zone_active[i]) any_active = true;
    }
    if (!any_active) {
        time_t now = time(NULL);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        for (uint8_t i = 0; i < ZONE_COUNT; i++) {
            zone_config_t *z = &g_config.zones[i];
            if (!z->enabled) continue;
            if (!is_day_match(z->days)) continue;
            if (tm_now.tm_hour == z->startHour && tm_now.tm_min == z->startMinute) {
                zone_set(i, true);
                ESP_LOGI(TAG, "  START strefa %d (%s) czas: %d min", i, z->name, z->duration / 60);
                break; // one zone at a time
            }
        }
    }

    zones_unlock();
}

bool zone_is_active(uint8_t z)
{
    if (z >= ZONE_COUNT) return false;
    return s_zone_active[z];
}

int zone_remaining(uint8_t z)
{
    if (z >= ZONE_COUNT) return 0;
    if (!s_zone_active[z]) return 0;
    // RAW, unclamped — matches original /api/zones output exactly. Uses the
    // transient run override when set, else the persisted schedule duration.
    return (int)effective_duration(z) - (int)((millis_now() - s_zone_started[z]) / 1000);
}

bool rain_detected(void)
{
    return s_rain_detected;
}
