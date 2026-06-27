// web_server.c — WP3: esp_http_server implementation.
//
// Ports setupWebServer()/sendZoneStatus()/sendScheduleConfig()/handleZoneCommand()
// the SSE block in loop(), and the OTA handler from the Arduino src/main.cpp to
// native ESP-IDF. JSON OUTPUT is hand-written via snprintf (exact byte-for-byte
// shapes the React frontend expects); cJSON is used ONLY to PARSE incoming POST
// bodies. CORS headers (wide open) are added to every response.

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "cJSON.h"

#include "app_config.h"
#include "zones.h"
#include "web_server.h"
#include "wifi_mgr.h"

static const char *TAG = "web";

// ─── WP4 dependency ──────────────────────────────────────────────────────────
// MQTT lives in WP4. We only need the connected flag for /api/info. Provide a
// weak fallback so this module links (and reports "false") even before WP4
// lands; WP4's strong definition takes over at link time.
bool mqtt_is_connected(void);
__attribute__((weak)) bool mqtt_is_connected(void) { return false; }

// ─── Server + SSE client tracking ───────────────────────────────────────────
static httpd_handle_t s_server = NULL;

// SSE socket fds are added by the /events handler and removed by the SSE push
// work function. BOTH run in the context of the single httpd server task
// (URI handlers and httpd_queue_work callbacks share that task), so this array
// needs no locking — it is only ever touched from that one task.
#define MAX_SSE_CLIENTS 6
static int s_sse_fds[MAX_SSE_CLIENTS];

static void sse_fds_init(void)
{
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) s_sse_fds[i] = -1;
}

static void sse_add_fd(int fd)
{
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (s_sse_fds[i] == fd) return;          // already tracked
    }
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (s_sse_fds[i] == -1) { s_sse_fds[i] = fd; return; }
    }
    ESP_LOGW(TAG, "SSE client table full; dropping fd %d", fd);
}

static void sse_remove_fd(int fd)
{
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (s_sse_fds[i] == fd) { s_sse_fds[i] = -1; return; }
    }
}

// ─── CORS / response helpers ────────────────────────────────────────────────
static void set_cors(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PUT");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

static esp_err_t send_json(httpd_req_t *req, const char *json)
{
    set_cors(req);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// Escape a string for safe embedding inside a JSON "..." literal. Handles the
// JSON-significant chars (" and \) and control chars; raw UTF-8 bytes (>=0x80,
// e.g. ą/ł/ó in zone names) pass through unchanged, which is valid JSON. Without
// this a name containing a quote would produce malformed JSON and break the UI.
static void json_escape(char *dst, size_t dstsize, const char *src)
{
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 7 < dstsize; i++) {
        unsigned char c = (unsigned char)src[i];
        switch (c) {
            case '"':  dst[o++] = '\\'; dst[o++] = '"';  break;
            case '\\': dst[o++] = '\\'; dst[o++] = '\\'; break;
            case '\n': dst[o++] = '\\'; dst[o++] = 'n';  break;
            case '\r': dst[o++] = '\\'; dst[o++] = 'r';  break;
            case '\t': dst[o++] = '\\'; dst[o++] = 't';  break;
            default:
                if (c < 0x20) o += snprintf(dst + o, dstsize - o, "\\u%04x", c);
                else          dst[o++] = (char)c;
                break;
        }
    }
    dst[o] = '\0';
}

// ─── JSON builders (hand-written, mirror sendZoneStatus/sendScheduleConfig) ──
static void build_zone_status(char *buf, size_t n)
{
    int off = 0;
    off += snprintf(buf + off, n - off, "[");
    for (int i = 0; i < ZONE_COUNT; i++) {
        bool active = zone_is_active(i);
        char ename[160];
        json_escape(ename, sizeof(ename), g_config.zones[i].name);
        off += snprintf(buf + off, n - off,
            "%s{\"id\":%d,\"name\":\"%s\",\"active\":%s,\"remaining\":%d,\"enabled\":%s}",
            i > 0 ? "," : "",
            i, ename,
            active ? "true" : "false",
            zone_remaining(i),                       // 0 for inactive (matches original)
            g_config.zones[i].enabled ? "true" : "false");
    }
    snprintf(buf + off, n - off, "]");
}

static void build_schedule(char *buf, size_t n)
{
    int off = 0;
    off += snprintf(buf + off, n - off, "[");
    for (int i = 0; i < ZONE_COUNT; i++) {
        zone_config_t *z = &g_config.zones[i];
        char ename[160];
        json_escape(ename, sizeof(ename), z->name);
        off += snprintf(buf + off, n - off,
            "%s{\"id\":%d,\"name\":\"%s\",\"enabled\":%s,\"days\":%d,\"startHour\":%d,\"startMinute\":%d,\"duration\":%d}",
            i > 0 ? "," : "",
            i, ename, z->enabled ? "true" : "false",
            z->days, z->startHour, z->startMinute, z->duration);
    }
    snprintf(buf + off, n - off, "]");
}

// ─── GET /api/zones ─────────────────────────────────────────────────────────
static esp_err_t h_zones(httpd_req_t *req)
{
    char buf[1024];
    build_zone_status(buf, sizeof(buf));
    return send_json(req, buf);
}

// ─── GET /api/schedule ──────────────────────────────────────────────────────
static esp_err_t h_schedule(httpd_req_t *req)
{
    char buf[1024];
    build_schedule(buf, sizeof(buf));
    return send_json(req, buf);
}

// ─── GET /api/rain ──────────────────────────────────────────────────────────
static esp_err_t h_rain(httpd_req_t *req)
{
    char buf[24];
    snprintf(buf, sizeof(buf), "{\"rain\":%s}", rain_detected() ? "true" : "false");
    return send_json(req, buf);
}

// ─── GET /api/zone/{0..5}/command?action=on|off|run&seconds=N ───────────────
static esp_err_t h_zone_command(httpd_req_t *req)
{
    int zone = -1;
    sscanf(req->uri, "/api/zone/%d/command", &zone);

    char query[64];
    char action[16] = {0};
    char seconds[16] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        httpd_query_key_value(query, "action", action, sizeof(action));
        httpd_query_key_value(query, "seconds", seconds, sizeof(seconds));
    }

    if (zone >= 0 && zone < ZONE_COUNT && action[0]) {
        if (strcmp(action, "on") == 0) {
            zone_set((uint8_t)zone, true);
        } else if (strcmp(action, "off") == 0) {
            zone_set((uint8_t)zone, false);
        } else if (strcmp(action, "run") == 0 && seconds[0]) {
            int sec = atoi(seconds);
            if (sec > 0 && sec < 3600) {
                zone_run_for((uint8_t)zone, (uint16_t)sec);
            }
        }
    }

    char buf[1024];
    build_zone_status(buf, sizeof(buf));
    return send_json(req, buf);
}

// ─── GET /api/all/off ───────────────────────────────────────────────────────
static esp_err_t h_all_off(httpd_req_t *req)
{
    all_zones_off();
    return send_json(req, "{\"ok\":true}");
}

// ─── GET /api/info ──────────────────────────────────────────────────────────
static esp_err_t h_info(httpd_req_t *req)
{
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M %d-%m-%Y", &tmv);

    char ip[16];
    wifi_get_ip(ip, sizeof(ip));

    char buf[384];
    snprintf(buf, sizeof(buf),
        "{\"version\":\"%s\",\"time\":\"%s\",\"synced\":%s,\"uptime\":%lu,\"heap\":%u,\"mqtt\":%s,\"rain\":%s,"
        "\"wifiMode\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\"}",
        FIRMWARE_VERSION, timeBuf,
        g_time_synced ? "true" : "false",
        (unsigned long)(esp_timer_get_time() / 1000000),
        (unsigned)esp_get_free_heap_size(),
        mqtt_is_connected() ? "true" : "false",
        rain_detected() ? "true" : "false",
        wifi_mode_str(), ip, wifi_ssid());
    return send_json(req, buf);
}

// ─── Read a full request body into a heap buffer (NUL-terminated) ───────────
// Returns malloc'd buffer (caller frees) or NULL. Caps at `cap` bytes.
static char *recv_body(httpd_req_t *req, size_t cap)
{
    size_t total = req->content_len;
    if (total == 0 || total > cap) return NULL;
    char *body = malloc(total + 1);
    if (!body) return NULL;

    size_t off = 0;
    while (off < total) {
        int r = httpd_req_recv(req, body + off, total - off);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) { free(body); return NULL; }
        off += r;
    }
    body[total] = '\0';
    return body;
}

// ─── POST /api/save (body: JSON array of schedule objects) ──────────────────
static esp_err_t h_save(httpd_req_t *req)
{
    char *body = recv_body(req, 4096);
    if (!body) {
        set_cors(req);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"error\":\"bad json\"}", HTTPD_RESP_USE_STRLEN);
    }

    cJSON *arr = cJSON_Parse(body);
    free(body);
    if (!arr || !cJSON_IsArray(arr)) {
        if (arr) cJSON_Delete(arr);
        set_cors(req);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"error\":\"bad json\"}", HTTPD_RESP_USE_STRLEN);
    }

    int count = cJSON_GetArraySize(arr);
    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_GetArrayItem(arr, i);
        cJSON *jid = cJSON_GetObjectItem(item, "id");
        if (!cJSON_IsNumber(jid)) continue;
        int id = jid->valueint;
        if (id < 0 || id >= ZONE_COUNT) continue;
        zone_config_t *z = &g_config.zones[id];

        cJSON *jname = cJSON_GetObjectItem(item, "name");
        if (cJSON_IsString(jname) && jname->valuestring) {
            strncpy(z->name, jname->valuestring, sizeof(z->name) - 1);
            z->name[sizeof(z->name) - 1] = '\0';
        }
        cJSON *v;
        if ((v = cJSON_GetObjectItem(item, "enabled")))     z->enabled     = cJSON_IsTrue(v);
        if ((v = cJSON_GetObjectItem(item, "days")))        z->days        = (uint8_t)v->valueint;
        if ((v = cJSON_GetObjectItem(item, "startHour")))   z->startHour   = (uint8_t)v->valueint;
        if ((v = cJSON_GetObjectItem(item, "startMinute"))) z->startMinute = (uint8_t)v->valueint;
        if ((v = cJSON_GetObjectItem(item, "duration")))    z->duration    = (uint16_t)v->valueint;
    }
    cJSON_Delete(arr);
    config_save();

    return send_json(req, "{\"ok\":true}");
}

// ─── POST /api/wifi (body {ssid,pass}) → save + reboot ──────────────────────
static esp_err_t h_wifi(httpd_req_t *req)
{
    char *body = recv_body(req, 512);
    if (!body) {
        set_cors(req);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"error\":\"bad json\"}", HTTPD_RESP_USE_STRLEN);
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (!root) {
        set_cors(req);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req, "{\"error\":\"bad json\"}", HTTPD_RESP_USE_STRLEN);
    }

    cJSON *ssid = cJSON_GetObjectItem(root, "ssid");
    cJSON *pass = cJSON_GetObjectItem(root, "pass");
    strncpy(g_config.wifiSsid, (cJSON_IsString(ssid) && ssid->valuestring) ? ssid->valuestring : "", 32);
    g_config.wifiSsid[sizeof(g_config.wifiSsid) - 1] = '\0';
    strncpy(g_config.wifiPass, (cJSON_IsString(pass) && pass->valuestring) ? pass->valuestring : "", 64);
    g_config.wifiPass[sizeof(g_config.wifiPass) - 1] = '\0';
    cJSON_Delete(root);
    config_save();

    send_json(req, "{\"ok\":true}");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;  // not reached
}

// ─── POST /api/ota (stream .bin into OTA partition) ─────────────────────────
static esp_err_t h_ota(httpd_req_t *req)
{
    esp_err_t err = ESP_OK;
    const esp_partition_t *update = esp_ota_get_next_update_partition(NULL);
    esp_ota_handle_t handle = 0;
    bool ok = (update != NULL);

    if (ok) {
        err = esp_ota_begin(update, OTA_SIZE_UNKNOWN, &handle);
        if (err != ESP_OK) { ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err)); ok = false; }
    }

    if (ok) {
        char *buf = malloc(2048);
        if (!buf) { esp_ota_end(handle); ok = false; }
        else {
            int remaining = req->content_len;
            while (remaining > 0) {
                int r = httpd_req_recv(req, buf, remaining > 2048 ? 2048 : remaining);
                if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
                if (r <= 0) { ok = false; break; }
                if (esp_ota_write(handle, buf, r) != ESP_OK) { ok = false; break; }
                remaining -= r;
            }
            free(buf);
            if (esp_ota_end(handle) != ESP_OK) ok = false;
            if (ok && esp_ota_set_boot_partition(update) != ESP_OK) ok = false;
        }
    }

    set_cors(req);
    httpd_resp_set_type(req, "text/plain");
    if (ok) {
        httpd_resp_send(req, "OTA OK", HTTPD_RESP_USE_STRLEN);
        ESP_LOGI(TAG, "OTA success — restarting...");
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, "OTA FAILED", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

// ─── OPTIONS /* — CORS preflight ────────────────────────────────────────────
static esp_err_t h_options(httpd_req_t *req)
{
    set_cors(req);
    return httpd_resp_send(req, NULL, 0);
}

// ─── Static files from SPIFFS (catch-all GET /*) ────────────────────────────
static const char *content_type_for(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "text/plain";
    if (!strcmp(dot, ".html")) return "text/html";
    if (!strcmp(dot, ".css"))  return "text/css";
    if (!strcmp(dot, ".js"))   return "application/javascript";
    if (!strcmp(dot, ".json")) return "application/json";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcmp(dot, ".ico"))  return "image/x-icon";
    if (!strcmp(dot, ".png"))  return "image/png";
    return "text/plain";
}

static esp_err_t h_static(httpd_req_t *req)
{
    char path[256];
    // req->uri may carry a query string; cut it off.
    const char *uri = req->uri;
    size_t uri_len = strcspn(uri, "?");

    if (uri_len == 1 && uri[0] == '/') {
        snprintf(path, sizeof(path), "/spiffs/index.html");
    } else {
        snprintf(path, sizeof(path), "/spiffs%.*s", (int)uri_len, uri);
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        set_cors(req);
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "text/plain");
        return httpd_resp_send(req, "Not Found", HTTPD_RESP_USE_STRLEN);
    }

    set_cors(req);
    httpd_resp_set_type(req, content_type_for(path));

    const size_t CHUNK = 4096;
    char *chunk = malloc(CHUNK);
    if (!chunk) { fclose(f); httpd_resp_send_500(req); return ESP_FAIL; }
    size_t n;
    do {
        n = fread(chunk, 1, CHUNK, f);
        if (n > 0) {
            if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
                free(chunk);
                fclose(f);
                return ESP_FAIL;
            }
        }
    } while (n == CHUNK);
    free(chunk);
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);   // terminate
    return ESP_OK;
}

// ─── SSE /events ────────────────────────────────────────────────────────────
// Bypass the normal httpd response path: write the event-stream HTTP header
// directly to the socket and keep it open. The fd is then fed by the SSE push
// work function in web_server_sse_tick().
static esp_err_t h_events(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    if (fd < 0) return ESP_FAIL;

    static const char preamble[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: keep-alive\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    if (httpd_socket_send(s_server, fd, preamble, sizeof(preamble) - 1, 0) < 0) {
        return ESP_FAIL;
    }
    sse_add_fd(fd);
    return ESP_OK;   // leave socket open for streaming
}

// SSE push work — runs on the httpd server task (via httpd_queue_work). `arg`
// is a heap-allocated, NUL-terminated SSE frame; we own it and free it here.
static void sse_push_work(void *arg)
{
    char *frame = (char *)arg;
    size_t len = strlen(frame);
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        int fd = s_sse_fds[i];
        if (fd < 0) continue;
        if (httpd_socket_send(s_server, fd, frame, len, 0) < 0) {
            sse_remove_fd(fd);   // client gone
        }
    }
    free(frame);
}

void web_server_sse_tick(void)
{
    if (!s_server) return;

    static int64_t last_us = 0;
    int64_t now = esp_timer_get_time();
    if (now - last_us < 2000000) return;   // ~2 s cadence
    last_us = now;

    // Build payload {"zones":[{"id":i,"a":bool[,"r":rem]}],"rain":bool}
    // ("r" included only when the zone is active, clamped >= 0 — matches original)
    char json[512];
    int off = 0;
    off += snprintf(json + off, sizeof(json) - off, "{\"zones\":[");
    for (int i = 0; i < ZONE_COUNT; i++) {
        bool a = zone_is_active(i);
        if (a) {
            int r = zone_remaining(i);
            if (r < 0) r = 0;
            off += snprintf(json + off, sizeof(json) - off,
                "%s{\"id\":%d,\"a\":true,\"r\":%d}", i > 0 ? "," : "", i, r);
        } else {
            off += snprintf(json + off, sizeof(json) - off,
                "%s{\"id\":%d,\"a\":false}", i > 0 ? "," : "", i);
        }
    }
    off += snprintf(json + off, sizeof(json) - off,
        "],\"rain\":%s}", rain_detected() ? "true" : "false");

    // Wrap as an SSE 'update' frame and hand off to the httpd task.
    char *frame = malloc(sizeof(json) + 32);
    if (!frame) return;
    snprintf(frame, sizeof(json) + 32, "event: update\ndata: %s\n\n", json);

    if (httpd_queue_work(s_server, sse_push_work, frame) != ESP_OK) {
        free(frame);
    }
}

// ─── Registration ───────────────────────────────────────────────────────────
static void reg(httpd_handle_t s, const char *uri, httpd_method_t method,
                esp_err_t (*handler)(httpd_req_t *))
{
    httpd_uri_t u = { .uri = uri, .method = method, .handler = handler, .user_ctx = NULL };
    httpd_register_uri_handler(s, &u);
}

void web_server_start(void)
{
    sse_fds_init();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 20;
    config.uri_match_fn     = httpd_uri_match_wildcard;
    config.max_open_sockets = 12;       // needs CONFIG_LWIP_MAX_SOCKETS=16 (3 reserved)
    config.lru_purge_enable = true;     // recycle the oldest socket when full
    config.stack_size       = 8192;     // handlers use ~2 KB stack buffers

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    // API routes first (specific before wildcards).
    reg(s_server, "/api/zones",        HTTP_GET,  h_zones);
    reg(s_server, "/api/schedule",     HTTP_GET,  h_schedule);
    reg(s_server, "/api/rain",         HTTP_GET,  h_rain);
    reg(s_server, "/api/all/off",      HTTP_GET,  h_all_off);
    reg(s_server, "/api/info",         HTTP_GET,  h_info);
    reg(s_server, "/api/zone/*",       HTTP_GET,  h_zone_command);
    reg(s_server, "/api/save",         HTTP_POST, h_save);
    reg(s_server, "/api/wifi",         HTTP_POST, h_wifi);
    reg(s_server, "/api/ota",          HTTP_POST, h_ota);
    reg(s_server, "/events",           HTTP_GET,  h_events);
    reg(s_server, "/*",                HTTP_OPTIONS, h_options);  // CORS preflight
    reg(s_server, "/*",                HTTP_GET,  h_static);      // catch-all static, LAST

    ESP_LOGI(TAG, "HTTP server started");
}
