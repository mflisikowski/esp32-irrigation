# CLAUDE.md

## What this is

ESP32-C6 garden irrigation controller: **native ESP-IDF firmware in pure C** (`main/`) that serves a React/TypeScript web UI (`web/`) from SPIFFS, controls 6 valve zones via relays, and exposes an HTTP API + MQTT. User-facing strings and most docs are in Polish.

Note: the firmware lives at the **repo root** (ESP-IDF project: root `CMakeLists.txt` + `main/`), not in a `firmware/` subfolder. Some older README/`.gitignore` wording may still reference `firmware/` — paths are relative to the repo root.

History: the firmware was migrated from the Arduino framework (single `src/main.cpp` using ESPAsyncWebServer/PubSubClient/ArduinoJson/Preferences) to native ESP-IDF. The Arduino sources and PlatformIO config were removed; see git history for the pre-migration version.

## Commands

### Firmware (ESP-IDF, v5.3+)
First load the environment once per shell: `. $HOME/esp/esp-idf/export.sh`
- **Dual-target.** The intended final hardware is the **ESP32-C6** (`idf.py set-target esp32c6`), but the current dev board is a **classic ESP32** (`idf.py set-target esp32`). Pick per board; `set-target` rewrites `sdkconfig` and clears the build. GPIO pins are selected per target in `main/zones.c` (`#if CONFIG_IDF_TARGET_ESP32C6`). Both Xtensa (`esp32`) and RISC-V (`esp32c6`) toolchains are installed.
- `idf.py build` — build firmware **and** the web UI + SPIFFS image
- `idf.py -p <PORT> flash` — flash app + partition table + SPIFFS over USB
- `idf.py -p <PORT> monitor` — serial monitor @ 115200 baud (Ctrl-] to exit)
- `idf.py -p <PORT> flash monitor` — flash then monitor
- `idf.py erase-flash` — full flash erase (needed when changing partition layout)
- `idf.py menuconfig` — interactive sdkconfig (defaults live in `sdkconfig.defaults`)

The root `CMakeLists.txt` defines a `web_build` custom target (`npm run build` in `web/` → copy `web/build` → `data/`); `spiffs_create_partition_image(spiffs data FLASH_IN_PROJECT)` then bakes `data/` into the SPIFFS image. So `idf.py build` rebuilds the web UI automatically. The `web_build` step is non-fatal (falls back to existing `data/`) so an isolated firmware build still works if npm is unavailable.

### Web UI (run inside `web/`)
- One-time: `npm install` (a stale global pnpm `tsc` shim can shadow the local one — the build uses the local TypeScript in `web/node_modules`).
- `npm run dev` — Vite dev server. Proxies `/api` to a hardcoded device IP in `web/vite.config.ts` (update that IP to match your device).
- `npm run build` — `tsc` typecheck + Vite build into `web/build/`, then flattens assets into `build/static/`.

There is no test suite.

## Architecture

### Firmware (`main/`, modular C)
- **`app_main.c`** — wiring only: init NVS, confirm pending-verify OTA image, `esp_netif_init` + default event loop, mount SPIFFS (`/spiffs`, label `spiffs`), `config_load()`, `zones_gpio_init()`, then init sibling modules. A FreeRTOS task replaces the Arduino `loop()`: `check_rain_sensor()` + `check_schedules()` + `time_sync_tick()` + `mqtt_tick()` + `web_server_sse_tick()` every ~250 ms. Each `*_tick()` **self-rate-limits** internally (NTP hourly, MQTT 15 s, SSE 2 s) — `app_main` owns no timers.
- **`app_config.{c,h}`** — `app_config_t` (6 `zone_config_t` + WiFi/MQTT/flags) persisted as raw bytes via `nvs_*_blob` under namespace `irrig`, key `config`. `config_load()` falls back to `DEFAULT_CONFIG` on size mismatch — **changing struct layout invalidates saved config** and resets to defaults. Exposes the global `g_config` and the `volatile bool g_time_synced` flag.
- **`zones.{c,h}`** — relay GPIO (active-LOW, `RELAY_ON=0`), zone state, scheduling engine, rain sensor. `check_schedules()`: time-of-day + day-of-week bitmask (`0=Sun..6=Sat`), **one zone at a time**, auto-stop after `duration` (s), gated on `g_time_synced`; rain (`rain_detected()` + `config.rainOverride`) suppresses starts and force-stops. Getters `zone_is_active`/`zone_remaining`/`rain_detected` feed web/mqtt/sse.
- **`wifi_mgr.{c,h}`** — event-driven `esp_wifi` STA from stored creds; on failure/no-SSID falls back to softAP `Nawodnienie-AP`. Does NOT re-init netif/event loop (app_main did).
- **`time_sync.{c,h}`** — `esp_netif_sntp` with `pool.ntp.org`, TZ `CET-1CEST,M3.5.0/2,M10.5.0/3`; sets `g_time_synced`; hourly resync in `time_sync_tick()`.
- **`web_server.{c,h}`** — `esp_http_server` on port 80. JSON output hand-written via `snprintf` (cJSON parses POST bodies only). One wildcard `/api/zone/*` handler (`httpd_uri_match_wildcard`, `max_uri_handlers` raised at runtime). Static files from `/spiffs` via a catch-all `/*` handler. CORS wide open (`*`). SSE at `/events`: keeps open socket fds, pushes `{zones,rain}` every 2 s via `httpd_queue_work` + `httpd_socket_send`. OTA `POST /api/ota` streams into `esp_ota_*`.
- **`mqtt_client.{c,h}`** — optional (`config.mqttEnabled`), `esp-mqtt` (auto-reconnect). Subscribes `<topic>/zone/{id}/command` + `irrigation/all/command`; publishes zone status/duration + `irrigation/rain` retained every 15 s. **No** Home Assistant auto-discovery. Header note: the local `mqtt_client.h` collides by name with esp-mqtt's; the `.c` reaches the framework via `<mqtt5_client.h>` — keep that workaround.

### Web UI (`web/src/`)
React 19 + Vite. `App.tsx` is the root and owns all state; it polls `getZones`/`getSchedule`/`getInfo` every 5 s (does not consume SSE). `api/esp32.ts` is the axios client (baseURL `/api`). Shared types in `types/index.ts` must stay in sync with the JSON shapes emitted by `web_server.c`.

### Partition table (critical for flashing)
`partitions.csv` is the OTA-capable 4 MB layout (dual app slots + SPIFFS + coredump), wired via `CONFIG_PARTITION_TABLE_CUSTOM` in `sdkconfig.defaults`. `idf.py erase-flash` before flashing if you change it. See `docs/OTA_PARTITION_PLAN.md` for the 4 MB flash-size constraint.

## Conventions
- 6 zones is hardcoded via `ZONE_COUNT` (in `app_config.h`); adding/removing zones touches `RELAY_PINS`, `DEFAULT_CONFIG`, and the React UI (web routes are a single wildcard now).
- **GPIO pins** are target-conditional in `zones.c`: classic ESP32 → `{32,33,25,26,27,14}` + rain `34` (original wiring); ESP32-C6 → `{0,1,2,3,10,11}` + rain `4`. `zones_gpio_init` still guards out-of-range pins. (Note: the rain pin is input-only on classic ESP32 — the internal-pullup request logs a harmless error; use an external pull-up.)
- When changing any API JSON shape in `web_server.c`, update `web/src/types/index.ts` and `web/src/api/esp32.ts` to match.
- `data/` (generated web build) is gitignored; do not edit it by hand — it's regenerated from `web/build`. Also gitignore `build/` and `sdkconfig`.
