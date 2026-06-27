// wifi_mgr.c — WP2 (connectivity)
//
// Port of the Arduino setup() WiFi block to native ESP-IDF (esp_wifi +
// esp_netif + esp_event). STA connect from stored creds with an AP fallback,
// preserving the original ~10 s connect window (Arduino: 20 x 500 ms) and the
// open softAP "Nawodnienie-AP". Polish serial messages are kept verbatim.

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mdns.h"

#include "app_config.h"
#include "wifi_mgr.h"

static const char *TAG = "wifi";

// Mirror of the Arduino loop: 20 attempts x 500 ms ~= 10 s total connect window.
#define WIFI_MAX_RETRY      20
#define WIFI_CONNECT_TIMEOUT_MS 10000
#define WIFI_AP_SSID        "Nawodnienie-AP"

// Event group bits set from the WiFi/IP event handler.
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_events;
static esp_netif_t       *s_sta_netif;
static esp_netif_t       *s_ap_netif;
static int                s_retry_num;
static volatile bool      s_connected;
static volatile bool      s_ap_mode;

bool wifi_is_connected(void)
{
    return s_connected;
}

const char *wifi_mode_str(void)
{
    return s_ap_mode ? "AP" : "STA";
}

const char *wifi_ssid(void)
{
    return s_ap_mode ? WIFI_AP_SSID : g_config.wifiSsid;
}

void wifi_get_ip(char *buf, size_t len)
{
    esp_netif_t *nif = s_ap_mode ? s_ap_netif : s_sta_netif;
    esp_netif_ip_info_t ip_info;
    if (nif && esp_netif_get_ip_info(nif, &ip_info) == ESP_OK) {
        snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    } else {
        snprintf(buf, len, "0.0.0.0");
    }
}

// Advertise the device as "irrigation.local" + an _http._tcp service on port 80
// so the web UI is reachable by name regardless of the DHCP-assigned IP. Works
// in both STA and softAP mode. Non-fatal: a failure just leaves IP-only access.
static void start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "  mDNS init fail: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set("irrigation");
    mdns_instance_name_set("Nawodnienie");
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "  mDNS: http://irrigation.local");
}

static void wifi_event_handler(void *arg, esp_event_base_t base,
                               int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_connected = false;
        if (s_retry_num < WIFI_MAX_RETRY) {
            s_retry_num++;
            esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_wifi_events, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)data;
        ESP_LOGI(TAG, "  WiFi OK: " IPSTR " (%s)",
                 IP2STR(&event->ip_info.ip), g_config.wifiSsid);
        s_retry_num = 0;
        s_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED_BIT);
    }
}

// Bring up the open softAP fallback (WiFi.mode(WIFI_AP) + softAP(...)).
static void start_softap(void)
{
    if (s_ap_netif == NULL) {
        s_ap_netif = esp_netif_create_default_wifi_ap();
    }

    wifi_config_t ap_cfg = {0};
    strncpy((char *)ap_cfg.ap.ssid, WIFI_AP_SSID, sizeof(ap_cfg.ap.ssid) - 1);
    ap_cfg.ap.ssid_len      = strlen(WIFI_AP_SSID);
    ap_cfg.ap.channel       = 1;
    ap_cfg.ap.max_connection = 4;
    ap_cfg.ap.authmode      = WIFI_AUTH_OPEN;   // open AP, like WiFi.softAP(ssid)

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_ap_mode = true;

    esp_netif_ip_info_t ip_info;
    if (esp_netif_get_ip_info(s_ap_netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "  AP IP: " IPSTR, IP2STR(&ip_info.ip));
    }
}

// Attempt STA association; return true on success within the connect window.
static bool start_sta(void)
{
    s_sta_netif = esp_netif_create_default_wifi_sta();

    wifi_config_t sta_cfg = {0};
    strncpy((char *)sta_cfg.sta.ssid, g_config.wifiSsid, sizeof(sta_cfg.sta.ssid) - 1);
    strncpy((char *)sta_cfg.sta.password, g_config.wifiPass, sizeof(sta_cfg.sta.password) - 1);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Disable WiFi modem sleep. With power save on (the default WIFI_PS_MIN_MODEM)
    // every TCP round-trip waits for the next DTIM beacon (~100ms), making the
    // web UI crawl (a 431-byte index.html took ~5.5s). The controller is mains
    // powered, so trade idle power for a responsive server.
    esp_wifi_set_ps(WIFI_PS_NONE);

    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void wifi_mgr_init(void)
{
    s_wifi_events = xEventGroupCreate();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    if (strlen(g_config.wifiSsid) > 0) {
        if (start_sta()) {
            start_mdns();   // connected; IP already logged from the event handler
            return;
        }
        ESP_LOGW(TAG, "  WiFi FAIL — uruchamiam AP...");
        // Drop the failed STA attempt before switching to softAP.
        esp_wifi_stop();
    }

    start_softap();
    start_mdns();
}
