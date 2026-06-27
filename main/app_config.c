// app_config.c — config persistence (NVS blob), ported 1:1 from src/main.cpp.

#include "app_config.h"

#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char *TAG = "config";

app_config_t g_config;
volatile bool g_time_synced = false;

// DEFAULT_CONFIG — identical values/order to the Arduino DEFAULT_CONFIG.
static const app_config_t DEFAULT_CONFIG = {
    .zones = {
        { "Lewa",            true, 0x3F /*0b0111111*/,  6,  0, 600 },
        { "Prawa",           true, 0x3F /*0b0111111*/,  6, 10, 600 },
        { "Kroplująca lewa", true, 0x3F /*0b0111111*/,  7,  0, 480 },
        { "Tył",             true, 0x12 /*0b0010010*/,  7, 30, 300 },
        { "Przód",           true, 0x12 /*0b0010010*/,  8,  0, 300 },
        { "Rabaty",          true, 0x3F /*0b0111111*/, 20,  0, 900 },
    },
    .rainOverride = true,
    .mqttEnabled  = 0,
    .mqttServer   = "192.168.1.100",
    .mqttPort     = 1883,
    .mqttUser     = "",
    .mqttPass     = "",
    .mqttTopic    = "irrigation",
    .flowEnabled  = false,
    .wifiSsid     = "",
    .wifiPass     = "",
};

void config_save(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_blob(h, CONFIG_NVS_KEY, &g_config, sizeof(g_config));
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "config_save failed: %s", esp_err_to_name(err));
    }
    nvs_close(h);
}

void config_load(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(CONFIG_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s — using defaults", esp_err_to_name(err));
        memcpy(&g_config, &DEFAULT_CONFIG, sizeof(g_config));
        return;
    }

    size_t len = sizeof(g_config);
    err = nvs_get_blob(h, CONFIG_NVS_KEY, &g_config, &len);

    // Fall back to defaults when the stored blob is missing or its size does
    // not match the current struct layout (mirrors loadConfig()).
    if (err != ESP_OK || len != sizeof(g_config)) {
        ESP_LOGW(TAG, "config blob missing/size-mismatch (len=%u, want=%u) — loading defaults",
                 (unsigned)len, (unsigned)sizeof(g_config));
        memcpy(&g_config, &DEFAULT_CONFIG, sizeof(g_config));
        nvs_close(h);
        config_save();
        return;
    }

    nvs_close(h);
    ESP_LOGI(TAG, "config loaded (%u bytes)", (unsigned)sizeof(g_config));
}
