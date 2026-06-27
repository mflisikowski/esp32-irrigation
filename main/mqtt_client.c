// mqtt_client.c — WP4: esp-mqtt client (native ESP-IDF)
//
// 1:1 behavioral port of the Arduino PubSubClient code in src/main.cpp:
//   - mqttCallback()   -> mqtt_event_handler() MQTT_EVENT_DATA branch
//   - reconnectMQTT()  -> mqtt_init() + esp-mqtt auto-reconnect; subscribe on
//                         MQTT_EVENT_CONNECTED
//   - publishMQTT()    -> mqtt_publish_status(), rate-limited from mqtt_tick()
//
// esp-mqtt is event-driven and auto-reconnects, so there is no manual reconnect
// loop. mqtt_tick() only drives the periodic (~15s) retained status publish.

// IMPORTANT — header name collision:
// The esp-mqtt component's public API header is ALSO named "mqtt_client.h".
// Because the `main` component's include dir is searched before esp-mqtt's,
// a bare `#include <mqtt_client.h>` / `"mqtt_client.h"` here resolves to OUR
// local header below, not the framework one — so the esp_mqtt_* API would be
// missing. We reach the framework API by including <mqtt5_client.h> (a uniquely
// named esp-mqtt header) which does `#include "mqtt_client.h"` from its OWN
// directory, so its quoted include picks up the REAL esp-mqtt mqtt_client.h.
#include <mqtt5_client.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"

#include "app_config.h"
#include "zones.h"

// Our own public declarations (mqtt_init / mqtt_tick / mqtt_is_connected).
// Resolves to main/mqtt_client.h since the current file's directory is searched
// first for quoted includes.
#include "mqtt_client.h"

static const char *TAG = "mqtt";

#define MQTT_PUBLISH_INTERVAL_US (15 * 1000 * 1000)  // ~15s, mirrors publishMQTT cadence

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;
static int64_t s_last_publish_us = 0;

// ─── Subscribe (on connect) ──────────────────────────────────────────────────
// Mirrors reconnectMQTT()'s subscribe block.
static void mqtt_subscribe_all(esp_mqtt_client_handle_t client)
{
    char topic[64];
    for (int i = 0; i < ZONE_COUNT; i++) {
        snprintf(topic, sizeof(topic), "%s/zone/%d/command", g_config.mqttTopic, i);
        esp_mqtt_client_subscribe(client, topic, 0);
    }
    esp_mqtt_client_subscribe(client, "irrigation/all/command", 0);
}

// ─── Incoming message ────────────────────────────────────────────────────────
// Mirrors mqttCallback(): topic ".../zone/<id>/command" with payload ON/OFF,
// plus "irrigation/all/command" OFF -> all_zones_off().
static void mqtt_handle_data(const char *topic, int topic_len,
                             const char *data, int data_len)
{
    char tbuf[80];
    char pbuf[64];

    int tlen = topic_len < (int)sizeof(tbuf) - 1 ? topic_len : (int)sizeof(tbuf) - 1;
    memcpy(tbuf, topic, tlen);
    tbuf[tlen] = 0;

    int plen = data_len < (int)sizeof(pbuf) - 1 ? data_len : (int)sizeof(pbuf) - 1;
    memcpy(pbuf, data, plen);
    pbuf[plen] = 0;

    // "irrigation/all/command" -> OFF turns everything off (original subscribes it).
    if (strcmp(tbuf, "irrigation/all/command") == 0) {
        if (strcmp(pbuf, "OFF") == 0) {
            all_zones_off();
        }
        return;
    }

    // topic: <mqttTopic>/zone/<id>/command — match the variable prefix then id.
    // Original used sscanf("%*s/zone/%d/command"); "%*s" is greedy over
    // whitespace-delimited input, so match the trailing "/zone/<id>/command"
    // explicitly which is robust for any topic prefix.
    int zone = -1;
    const char *p = strstr(tbuf, "/zone/");
    if (p != NULL && sscanf(p, "/zone/%d/command", &zone) == 1) {
        if (zone >= 0 && zone < ZONE_COUNT) {
            if (strcmp(pbuf, "ON") == 0) {
                zone_set((uint8_t)zone, true);
            } else if (strcmp(pbuf, "OFF") == 0) {
                zone_set((uint8_t)zone, false);
            }
        }
    }
}

// ─── esp-mqtt event handler ──────────────────────────────────────────────────
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "MQTT connected");
            mqtt_subscribe_all(event->client);
            s_last_publish_us = 0;  // publish promptly after (re)connect
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        case MQTT_EVENT_DATA:
            mqtt_handle_data(event->topic, event->topic_len,
                             event->data, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT error");
            break;

        default:
            break;
    }
}

// ─── Periodic publish ────────────────────────────────────────────────────────
// Mirrors publishMQTT(): retained zone status/duration + global rain state.
static void mqtt_publish_status(void)
{
    char topic[64];
    char buf[16];

    for (int i = 0; i < ZONE_COUNT; i++) {
        snprintf(topic, sizeof(topic), "%s/zone/%d/status", g_config.mqttTopic, i);
        esp_mqtt_client_publish(s_client, topic,
                                zone_is_active((uint8_t)i) ? "ON" : "OFF",
                                0, 0, 1);

        snprintf(topic, sizeof(topic), "%s/zone/%d/duration", g_config.mqttTopic, i);
        snprintf(buf, sizeof(buf), "%u", (unsigned)g_config.zones[i].duration);
        esp_mqtt_client_publish(s_client, topic, buf, 0, 0, 1);
    }

    esp_mqtt_client_publish(s_client, "irrigation/rain",
                            rain_detected() ? "RAIN" : "DRY", 0, 0, 1);
}

// ─── Public API ──────────────────────────────────────────────────────────────
void mqtt_init(void)
{
    // Mirror reconnectMQTT() guards.
    if (!g_config.mqttEnabled) {
        ESP_LOGI(TAG, "MQTT disabled");
        return;
    }
    if (strlen(g_config.mqttServer) < 7) {
        ESP_LOGW(TAG, "MQTT server not configured");
        return;
    }
    if (s_client != NULL) {
        return;  // already started
    }

    // Stable 6-hex-digit client id from the WiFi STA MAC (replaces
    // ESP.getEfuseMac()>>24 from the original).
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    static char client_id[32];
    snprintf(client_id, sizeof(client_id), "esp32-irrigation-%02x%02x%02x",
             mac[3], mac[4], mac[5]);

    // Broker URI "mqtt://<server>:<port>".
    static char uri[64];
    snprintf(uri, sizeof(uri), "mqtt://%s:%u",
             g_config.mqttServer, (unsigned)g_config.mqttPort);

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri = uri;
    cfg.credentials.client_id = client_id;
    if (strlen(g_config.mqttUser) > 0) {
        cfg.credentials.username = g_config.mqttUser;
        cfg.credentials.authentication.password = g_config.mqttPass;
    }

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "esp_mqtt_client_init failed");
        return;
    }

    esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY,
                                   mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT connecting to %s (id=%s)", uri, client_id);
}

void mqtt_tick(void)
{
    if (s_client == NULL || !s_connected) {
        return;
    }

    int64_t now = esp_timer_get_time();
    if (now - s_last_publish_us < MQTT_PUBLISH_INTERVAL_US) {
        return;
    }
    s_last_publish_us = now;

    mqtt_publish_status();
}

bool mqtt_is_connected(void)
{
    return s_connected;
}
