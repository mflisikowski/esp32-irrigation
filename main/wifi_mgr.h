// wifi_mgr.h — WP2 (connectivity)
//
// WiFi bring-up that mirrors the Arduino setup() logic: connect in STA mode
// using the stored credentials (g_config.wifiSsid/wifiPass); on repeated
// failure (or when no SSID is stored) fall back to an open softAP named
// "Nawodnienie-AP". Event-driven via esp_wifi + esp_event.
//
// app_main owns esp_netif_init() and esp_event_loop_create_default(); this
// module only creates the default WiFi netif(s) it needs.

#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize WiFi and connect (STA) or start the fallback softAP. Blocking
// until the STA connection succeeds, the ~10 s timeout elapses, or the AP is
// up. Override of the WP1 weak stub of the same name (called from app_main).
void wifi_mgr_init(void);

// True once an IP has been obtained in STA mode. False while in softAP
// fallback or before association. Safe to read from other tasks.
bool wifi_is_connected(void);

// "STA" once associated to the configured network, else "AP" (softAP fallback).
const char *wifi_mode_str(void);

// SSID currently in use: the configured network in STA mode, or the softAP
// name ("Nawodnienie-AP") in fallback mode.
const char *wifi_ssid(void);

// Current IPv4 address as a dotted string ("0.0.0.0" if not yet assigned).
// In STA mode this is the address from the upstream DHCP; in AP mode it is the
// softAP gateway address. Writes at most `len` bytes (NUL-terminated).
void wifi_get_ip(char *buf, size_t len);

#ifdef __cplusplus
}
#endif
