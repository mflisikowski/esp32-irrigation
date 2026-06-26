# OTA Partition Fix Plan — ESP32 Irrigation System

**Created**: 2026-06-26
**Status**: Ready for implementation
**Current state**: `no_ota.csv` referenced but doesn't exist; `partitions.csv` has OTA layout but isn't used

---

## 1. Root Cause Analysis

### Primary Failure: Flash Size Overflow

The `partitions.csv` defines a layout requiring **6.25 MB** of flash:

| Partition | Offset | Size | End |
|-----------|--------|------|-----|
| nvs | 0x9000 | 0x5000 (20KB) | 0xE000 |
| otadata | 0xE000 | 0x2000 (8KB) | 0x10000 |
| app0 | 0x10000 | 0x200000 (2MB) | 0x210000 |
| app1 | 0x210000 | 0x200000 (2MB) | 0x410000 |
| spiffs | 0x410000 | 0x1E0000 (1.875MB) | 0x5F0000 |
| coredump | 0x5F0000 | 0x10000 (64KB) | 0x600000 |

**Total**: 0x600000 = **6,291,456 bytes = 6 MB**

The ESP32 board (per README: "ESP32 C6") almost certainly has **4 MB flash**. The partition table overflows by 2 MB.

When the bootloader reads this partition table, it finds partitions extending beyond flash chip boundaries → boot failure → device unresponsive.

### Secondary Issue: Missing `no_ota.csv`

`platformio.ini` line 7 references `no_ota.csv` but this file does **not exist** anywhere in the project. PlatformIO falls back to its built-in default partition table, which is a simple single-app layout without OTA. This worked accidentally — the default is a safe fallback.

### Why It Seemed to "Work" Before

When `board_build.partitions = no_ota.csv` was set:
- PlatformIO couldn't find the file
- It fell back to the **default ESP32 partition table** (single app, ~1.3MB, no OTA)
- This fit in 4MB flash → device worked
- OTA code existed in `main.cpp` but the `Update` library silently skipped operations because no OTA partitions existed

---

## 2. Correct Partition Layout for 4MB Flash + OTA + SPIFFS

### Recommended Layout

```csv
# ESP32 OTA Partition Table — 4MB Flash
# Name,   Type, SubType,  Offset,   Size,        Notes
nvs,      data, nvs,      0x9000,   0x5000,      # 20KB — WiFi config + NVS
otadata,  data, ota,      0xe000,   0x2000,      # 8KB — OTA state (2 sectors, power-fail safe)
app0,     app,  ota_0,    0x10000,  0x140000,    # 1.25MB — OTA slot 0
app1,     app,  ota_1,    0x150000, 0x140000,    # 1.25MB — OTA slot 1
spiffs,   data, spiffs,   0x290000, 0x160000,    # 1.375MB — Web UI assets
coredump, data, coredump, 0x3F0000, 0x10000,     # 64KB — Crash dumps
```

**Total**: 0x400000 = **4,194,304 bytes = 4 MB** ✓

### Size Breakdown

| Partition | Size | Purpose |
|-----------|------|---------|
| nvs | 20 KB | WiFi credentials, config |
| otadata | 8 KB | OTA boot tracking (mandatory) |
| app0 | 1.25 MB | Firmware slot A |
| app1 | 1.25 MB | Firmware slot B (OTA target) |
| spiffs | 1.375 MB | Web UI (React app ~252KB + margin) |
| coredump | 64 KB | Crash logs |

### Key Rules Met
- ✅ All app partition offsets are 64KB aligned (0x10000, 0x150000)
- ✅ All partition sizes are 4KB sector aligned
- ✅ `otadata` is exactly 0x2000 (2 sectors for power-fail protection)
- ✅ Total ≤ 4MB flash
- ✅ No partition overlaps
- ✅ SPIFFS has `data, spiffs` subtype

---

## 3. Implementation Plan

### Step 1: Verify Flash Size
```bash
# Connect ESP32 via USB, then:
esptool.py --port /dev/tty.usbserial-* flash_id
# Look for: "Detected flash size: 4MB" (or 8MB)
```

If 8MB: the original `partitions.csv` layout can be used (reduce app0/app1 to 1.5MB each to be safe).
If 4MB: use the corrected layout above.

### Step 2: Create Corrected `partitions.csv`

Replace `firmware/partitions.csv` with the 4MB layout from Section 2.

### Step 3: Update `platformio.ini`

Change line 7 from:
```ini
board_build.partitions = no_ota.csv
```
To:
```ini
board_build.partitions = partitions.csv
board_build.flash_size = 4MB
```

Also add to the `[env:esp32dev]` section:
```ini
board_build.partitions = partitions.csv
board_build.flash_size = 4MB
```

### Step 4: Erase Flash (CRITICAL)

Before uploading with new partition table, **erase the entire flash** to clear stale OTA data:
```bash
cd firmware
pio run --target erase
```

Or manually:
```bash
esptool.py --port /dev/tty.usbserial-* erase_flash
```

### Step 5: Upload Firmware

```bash
cd firmware
pio run --target upload
```

### Step 6: Verify Boot

```bash
pio device monitor --baud 115200
# Should see:
# "Irrigation Controller v..." startup message
# WiFi connection
# HTTP server started
```

### Step 7: Test OTA Update

1. Open React frontend → OTA Upload page
2. Upload a new firmware.bin (can be the same firmware for testing)
3. Verify device restarts and responds
4. Check serial monitor for "OTA Start" / "OTA End" messages

### Step 8: Upload SPIFFS

```bash
# Copy web build to SPIFFS data directory
cp -r firmware/web/build/* firmware/data/

# Upload filesystem
pio run --target uploadfs
```

---

## 4. OTA Code Improvements (Recommended)

The current OTA endpoint in `main.cpp` (lines 423-440) has no error handling. Add:

```cpp
// OTA firmware update
server.on("/api/ota", HTTP_POST,
  [](AsyncWebServerRequest *request) {
    if (Update.hasError()) {
      request->send(500, "text/plain", "OTA failed");
    } else {
      request->send(200, "text/plain", "OTA OK");
    }
  },
  [](AsyncWebServerRequest *request, String filename, size_t index,
     uint8_t *data, size_t len, bool final) {
      if (!index) {
        Serial.printf("OTA Start: %s\n", filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
          Update.printError(Serial);
        }
      }
      if (Update.write(data, len) != len) {
        Update.printError(Serial);
      }
      if (final) {
        Serial.printf("OTA End: %u bytes\n", index + len);
        if (!Update.end(true)) {
          Update.printError(Serial);
        } else {
          Serial.println("OTA success, restarting...");
          ESP.restart();
        }
      }
  }
);
```

Also add rollback confirmation in `setup()`:
```cpp
#include "esp_ota_ops.h"

void setup() {
  // ... existing code ...
  
  // Confirm this app is valid (prevents rollback)
  const esp_partition_t *running = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  if (esp_ota_get_state_partition(running, &state) == ESP_OK) {
    if (state == ESP_OTA_IMG_PENDING_VERIFY) {
      Serial.println("OTA verification pending, marking valid...");
      esp_ota_mark_app_valid_cancel_rollback();
    }
  }
  
  // ... rest of setup ...
}
```

---

## 5. Verification Checklist

| # | Test | Expected Result | Command |
|---|------|-----------------|---------|
| 1 | Flash size detected | 4MB | `esptool.py flash_id` |
| 2 | Build succeeds | firmware.bin created | `pio run` |
| 3 | Upload succeeds | Device reboots | `pio run -t upload` |
| 4 | Serial output shows startup | "Irrigation Controller v..." | `pio device monitor` |
| 5 | WiFi connects | IP assigned | Serial monitor |
| 6 | HTTP server responds | 200 on `/api/info` | `curl http://<ip>/api/info` |
| 7 | OTA upload works | Device restarts | Upload via React UI |
| 8 | OTA rollback works | Previous firmware boots | Upload invalid firmware → auto-rollback |
| 9 | SPIFFS accessible | Web UI loads | Open `http://<ip>/` in browser |

---

## 6. Rollback Strategy

### If OTA Upload Fails Again

1. **Immediate**: `pio run --target erase && pio run -t upload` (erase + re-flash)
2. **Serial monitor**: Check for error messages at 115200 baud
3. **Revert**: Change `platformio.ini` back to `board_build.partitions = no_ota.csv` (but create the file first — see Step 0 below)

### Step 0: Create `no_ota.csv` Safety Net

Before making any changes, create `firmware/no_ota.csv`:
```csv
# no_ota.csv — single app partition, no OTA
nvs,      data, nvs,      0x9000,   0x5000
app0,     app,  factory,  0x10000,  0x300000
spiffs,   data, spiffs,   0x310000, 0xE0000
```

This gives a 3MB app + 875KB SPIFFS on 4MB flash — a known-working fallback.

### Recovery Commands (copy-paste ready)

```bash
# Full recovery sequence
cd /Users/mateuszflisikowski/mfd-projects/opencode-desktop/irrigation/firmware

# 1. Erase flash
pio run --target erase

# 2. Upload with corrected partition table
pio run -t upload

# 3. Monitor for boot
pio device monitor --baud 115200

# If still broken, revert to no_ota:
# Edit platformio.ini: board_build.partitions = no_ota.csv
# pio run --target erase && pio run -t upload
```

---

## 7. File Changes Summary

| File | Action | Change |
|------|--------|--------|
| `firmware/partitions.csv` | **Replace** | Use 4MB layout from Section 2 |
| `firmware/platformio.ini` | **Edit** | Line 7: `partitions.csv`, add `flash_size = 4MB` |
| `firmware/no_ota.csv` | **Create** | Safety net fallback (Section 6) |
| `firmware/src/main.cpp` | **Edit** | Add OTA error handling (Section 4) |
| `firmware/data/` | **Populate** | `cp -r web/build/* data/` |

---

## 8. Risk Assessment

| Risk | Severity | Likelihood | Mitigation |
|------|----------|------------|------------|
| Flash size mismatch | High | Low (if verified) | Run `esptool.py flash_id` first |
| OTA upload corruption | Medium | Low | Error handling + rollback code |
| SPIFFS too small | Low | Low | 1.375MB is ample for 252KB web build |
| Power loss during OTA | Medium | Low | Dual-partition design prevents brick |
| Rollback not working | High | Low | Add `esp_ota_mark_app_valid_cancel_rollback()` |

---

## 9. Next Steps

1. **Verify flash size** (Section 3, Step 1)
2. **Create `no_ota.csv`** as safety net (Section 6)
3. **Update `partitions.csv`** with corrected 4MB layout
4. **Update `platformio.ini`** to reference `partitions.csv`
5. **Erase flash** before first upload
6. **Upload and verify** (Steps 5-6)
7. **Test OTA** (Step 7)
8. **Add error handling** to OTA code (Section 4)
9. **Upload SPIFFS** (Step 8)

