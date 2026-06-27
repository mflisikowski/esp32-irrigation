// time_sync.h — WP2 (connectivity)
//
// SNTP time sync that mirrors the Arduino configTzTime()/NTP block: kick off
// SNTP against pool.ntp.org, set the CET/CEST timezone, and maintain the shared
// g_time_synced flag (declared in app_config.h). The loop task drives the
// hourly resync via time_sync_tick().

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Set the timezone, start SNTP, and briefly wait for the first sync. Sets
// g_time_synced once the clock is plausible (epoch > 100000), mirroring the
// original `timeSynced = (now > 100000)`. Override of the WP1 weak stub.
void time_sync_init(void);

// Called every ~250 ms from the loop task. Self-rate-limited: roughly once an
// hour it re-checks the clock (and nudges SNTP) and refreshes g_time_synced,
// mirroring the loop()'s hourly getLocalTime() refresh. Override of the WP1
// weak stub.
void time_sync_tick(void);

#ifdef __cplusplus
}
#endif
