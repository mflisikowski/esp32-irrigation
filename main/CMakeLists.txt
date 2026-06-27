# WP1 compiles: app_main.c, app_config.c, zones.c
# Sibling packages add their sources here during integration (WP5):
#   "wifi_mgr.c"     (WP2 — WiFi STA/AP)
#   "time_sync.c"    (WP2 — SNTP + TZ)
#   "web_server.c"   (WP3 — esp_http_server: /api/*, static, SSE, OTA)
#   "mqtt_client.c"  (WP4 — esp-mqtt)
# Add them to SRCS below as they land.
idf_component_register(
    SRCS
        "app_main.c"
        "app_config.c"
        "zones.c"
        "wifi_mgr.c"      # WP2
        "time_sync.c"     # WP2
        "web_server.c"    # WP3
        "mqtt_client.c"   # WP4
    INCLUDE_DIRS "."
    # REQUIRES lists every component the FULL project needs so siblings can add
    # their sources without touching this file. WP1's own sources only use a
    # subset (nvs_flash, spiffs, driver, esp_timer, app_update, esp_partition,
    # esp_netif, esp_event).
    REQUIRES
        nvs_flash
        spiffs
        driver
        esp_wifi
        esp_http_server
        esp_netif
        esp_event
        mdns
        mqtt
        json
        esp_timer
        app_update
        esp_partition
        lwip
        esp_hw_support
)
