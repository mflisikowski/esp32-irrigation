// web_server.h — WP3 public contract (HTTP server).
//
// esp_http_server-based replacement for the Arduino ESPAsyncWebServer setup.
// Serves the React UI from SPIFFS, all /api/* endpoints (1:1 JSON shapes with
// the frontend), the /events SSE stream, and the /api/ota firmware updater.
//
// Both functions are the strong definitions for the WEAK stubs declared in
// app_main.c — signatures must match verbatim.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start httpd and register every route (API + static + SSE + OTA). Call once
// at startup, after SPIFFS is mounted and config is loaded.
void web_server_start(void);

// Called every ~250 ms from the loop task. Self-rate-limited to ~2 s: builds
// the compact {zones,rain} payload and pushes an SSE 'update' frame to every
// connected /events client. No-op until the rate-limit window elapses.
void web_server_sse_tick(void);

#ifdef __cplusplus
}
#endif
