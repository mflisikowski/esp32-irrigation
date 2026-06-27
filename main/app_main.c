// app_main.c — system wiring only (WP1 skeleton).
//
// Replicates the Arduino setup()/loop() lifecycle on ESP-IDF: init NVS + SPIFFS,
// confirm a pending-verify OTA image, load config, init zone GPIO, then call the
// sibling-module init hooks (provided here as WEAK stubs so WP1 links and runs
// standalone — WP2/WP3/WP4 override them with strong definitions). A FreeRTOS
// task replaces loop(): rain check, schedule engine, and periodic NTP/MQTT/SSE
// ticks delegated to sibling hooks. No business logic beyond wiring lives here.

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_spiffs.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "app_config.h"
#include "zones.h"

static const char *TAG = "app";

// ─── Sibling module interface (implemented by WP2/WP3/WP4) ───────────────────
// Strong definitions live in the sibling modules; app_main only declares them.
// (Earlier WP1 weak stubs were removed at integration: weak defaults in the same
// static archive prevent the linker from pulling the real objects, so the
// modules would be silently dead-stripped — caught on hardware. Plain externs
// force the linker to link the real sibling objects.)
#include "wifi_mgr.h"
#include "time_sync.h"
#include "web_server.h"
#include "mqtt_client.h"

// ─── Helpers ────────────────────────────────────────────────────────────────

static void nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void spiffs_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path              = "/spiffs",
        .partition_label        = "spiffs",
        .max_files              = 8,
        .format_if_mount_failed = true,   // mirrors SPIFFS.begin(true)
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
        return;
    }
    size_t total = 0, used = 0;
    if (esp_spiffs_info("spiffs", &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "SPIFFS: %u/%u bytes used", (unsigned)used, (unsigned)total);
    }
}

// After OTA the first boot is "pending verify" — confirm the image to avoid an
// automatic rollback on the next restart (ported from setup()).
static void ota_confirm_pending(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) == ESP_OK &&
        state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGI(TAG, "  OTA pending verify — oznaczam obraz jako poprawny");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

// ─── Main loop task (replaces Arduino loop()) ───────────────────────────────
static void irrigation_loop_task(void *arg)
{
    (void)arg;
    for (;;) {
        check_rain_sensor();
        check_schedules();      // internally gated on g_time_synced + rain
        time_sync_tick();       // hourly NTP resync (self-rate-limited)
        mqtt_tick();            // mqtt reconnect + 15s publish (self-rate-limited)
        web_server_sse_tick();  // 2s SSE push (self-rate-limited)
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "\n\n=== Irrigation Controller v%s ===", FIRMWARE_VERSION);

    nvs_init();
    ota_confirm_pending();

    // Network stack prerequisites for WiFi (WP2) and SNTP (WP2).
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    config_load();
    zones_gpio_init();
    spiffs_init();

    // Sibling modules (stubs until WP2/WP3/WP4 land).
    wifi_mgr_init();
    time_sync_init();
    web_server_start();
    mqtt_init();

    xTaskCreate(irrigation_loop_task, "irrig_loop", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "  System gotowy!");
}
