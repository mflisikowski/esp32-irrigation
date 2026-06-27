// time_sync.c — WP2 (connectivity)
//
// Port of the Arduino NTP block (configTzTime + the hourly getLocalTime resync
// in loop()) to native ESP-IDF using esp_netif_sntp / esp_sntp. Server is
// pool.ntp.org and the timezone is Poland's CET/CEST. The shared g_time_synced
// flag (app_config.h) is owned here.

#include <time.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"

#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "app_config.h"
#include "time_sync.h"

static const char *TAG = "time";

#define NTP_SERVER        "pool.ntp.org"
#define TZ_POLAND         "CET-1CEST,M3.5.0/2,M10.5.0/3"
// A plausible epoch threshold (matches the Arduino `now > 100000`).
#define EPOCH_PLAUSIBLE   100000
// Initial wait for first sync: 20 x 500 ms ~= 10 s, like the Arduino retry loop.
#define INITIAL_SYNC_TIMEOUT_MS 10000
// Hourly resync cadence (microseconds), mirroring loop()'s 3600000 ms guard.
#define RESYNC_INTERVAL_US (3600000ULL * 1000ULL)

static int64_t s_last_resync_us;

static void log_local_time(void)
{
    time_t now = time(NULL);
    struct tm tm_info;
    char buf[32];
    localtime_r(&now, &tm_info);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_info);
    ESP_LOGI(TAG, "  Czas: %s", buf);
}

void time_sync_init(void)
{
    // Timezone first so localtime conversions are correct once synced.
    setenv("TZ", TZ_POLAND, 1);
    tzset();

    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    cfg.start = true;          // begin polling immediately
    cfg.wait_for_sync = false; // we manage the wait ourselves below
    ESP_ERROR_CHECK(esp_netif_sntp_init(&cfg));

    // Short, bounded wait for the first sync (non-fatal on timeout).
    esp_netif_sntp_sync_wait(pdMS_TO_TICKS(INITIAL_SYNC_TIMEOUT_MS));

    time_t now = time(NULL);
    g_time_synced = (now > EPOCH_PLAUSIBLE);
    s_last_resync_us = esp_timer_get_time();

    if (g_time_synced) {
        log_local_time();
    } else {
        ESP_LOGW(TAG, "  NTP: brak synchronizacji (kontynuuję w tle)");
    }
}

void time_sync_tick(void)
{
    // Until the first sync lands, poll the clock cheaply on every tick so the
    // flag flips (and schedules can run) the moment SNTP completes in the
    // background — not up to an hour later. check_schedules() gates on this.
    if (!g_time_synced) {
        if (time(NULL) > EPOCH_PLAUSIBLE) {
            g_time_synced = true;
            s_last_resync_us = esp_timer_get_time();
            log_local_time();
        }
        return;
    }

    int64_t nowus = esp_timer_get_time();
    if (nowus - s_last_resync_us < (int64_t)RESYNC_INTERVAL_US) {
        return;   // self-rate-limit to ~hourly
    }
    s_last_resync_us = nowus;

    // SNTP keeps running in the background; nudge a fresh poll (mirrors loop()'s
    // hourly getLocalTime()).
    esp_netif_sntp_start();
}
