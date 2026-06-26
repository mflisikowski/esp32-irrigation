# CLAUDE.md

## What this is

ESP32 garden irrigation controller: C++/Arduino firmware (`src/main.cpp`) that serves a React/TypeScript web UI (`web/`) from SPIFFS, controls 6 valve zones via relays, and exposes an HTTP API + MQTT (Home Assistant auto-discovery). User-facing strings and most docs are in Polish.

Note: the firmware lives at the **repo root**, not in a `firmware/` subfolder. The README's instructions referencing `firmware/` and several `.gitignore` entries are stale — paths are relative to the repo root.

## Commands

### Firmware (PlatformIO)
- `pio run` — build (default env `esp32dev`)
- `pio run -t upload` — build + flash over USB
- `pio run -t uploadfs` — build web UI and flash SPIFFS (web assets)
- `pio device monitor` — serial monitor @ 115200 baud
- `pio run -t erase` — full flash erase (required when changing the partition table)
- `pio run -e esp32-c6 -t upload` — build/flash for the ESP32-C6 board (the intended target hardware)

A `pio run` triggers `extra_script.py`, which runs `npm run build` in `web/` and copies `web/build` → `data/` before building the SPIFFS image. So building/uploading firmware automatically rebuilds the web UI.

### Web UI (run inside `web/`)
- `npm run dev` — Vite dev server. Proxies `/api` to a hardcoded device IP in `web/vite.config.ts` (update that IP to match your device).
- `npm run build` — `tsc` typecheck + Vite build into `web/build/`, then flattens assets into `build/static/`.

There is no test suite (root `package.json` `test` is a placeholder).

## Architecture

### Firmware (`src/main.cpp`, single file)
- **Config**: a packed `Config` struct (6 `ZoneConfig` + WiFi/MQTT/flags) persisted as raw bytes via `Preferences` (NVS) under namespace `irrig`. `loadConfig()` falls back to `DEFAULT_CONFIG` if the stored blob size mismatches — **changing the struct layout invalidates saved config** and resets to defaults.
- **Scheduling** (`checkSchedules`, runs every `loop`): time-of-day + day-of-week bitmask (`0=Sun..6=Sat`) matching against each zone. Only **one zone runs at a time**; a zone auto-stops after its `duration` (seconds). Rain (`rainDetected` + `config.rainOverride`) suppresses schedule starts and force-stops zones.
- **Relays**: active-LOW (`RELAY_ON = LOW`). Pins in `RELAY_PINS[]`; rain sensor on pin 34 (`INPUT_PULLUP`, LOW = raining).
- **Web server** (`ESPAsyncWebServer` on port 80): JSON is hand-written via `printf` into response streams (no serializer on output; ArduinoJson only parses POST bodies). Per-zone command routes are registered individually (`/api/zone/0..5/command`). Static files served from SPIFFS root. CORS is wide open (`*`).
- **Live updates**: Server-Sent Events at `/events` push a compact `{zones,rain}` JSON every 2s. (The React app currently polls `/api/*` every 5s instead of consuming SSE.)
- **MQTT** (optional, `PubSubClient`): subscribes `…/zone/{id}/command` (`ON`/`OFF`), publishes zone status/duration + `irrigation/rain` retained every 15s.
- **OTA**: `POST /api/ota` streams a firmware image into `Update` and reboots. Requires an OTA-capable partition table (see below).
- **WiFi**: connects with stored creds; on failure starts AP `Nawodnienie-AP`. `POST /api/wifi` saves creds and reboots.

### Web UI (`web/src/`)
React 19 + Vite. `App.tsx` is the root and owns all state; it polls `getZones`/`getSchedule`/`getInfo` every 5s. `api/esp32.ts` is the axios client (baseURL `/api`). Components in `components/`, shared types in `types/index.ts` (must stay in sync with the JSON shapes emitted by `main.cpp`).

### Partition tables (critical for flashing)
`platformio.ini` references `no_ota.csv` (single 3MB app, no OTA — the safe default). `partitions.csv` is the OTA-capable 4MB layout (dual app slots + SPIFFS). **OTA does not work under `no_ota.csv`** — to enable OTA, point `board_build.partitions` at `partitions.csv` and `erase` before flashing. See `docs/OTA_PARTITION_PLAN.md` for the full rationale and the 4MB flash-size constraint.

## Conventions
- 6 zones is hardcoded via `ZONE_COUNT`; adding/removing zones touches the relay pins, default config, per-zone web routes, and the React UI.
- When changing any API JSON shape in `main.cpp`, update `web/src/types/index.ts` and `web/src/api/esp32.ts` to match.
- `data/` (generated web build) is gitignored; do not edit it by hand — it's regenerated from `web/build`.
