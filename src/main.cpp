#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <PubSubClient.h>
#include <time.h>
#include "SPIFFS.h"
#include <Update.h>

// ─── CONFIG ────────────────────────────────────────────────────────────────

#define ZONE_COUNT 6
#define RELAY_ON LOW
#define RELAY_OFF HIGH
#define FIRMWARE_VERSION "1.0.0"

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset = 3600;
const int   daylightOffset = 3600;

// ─── PINOUT ────────────────────────────────────────────────────────────────

const uint8_t RELAY_PINS[ZONE_COUNT] = { 32, 33, 25, 26, 27, 14 };
const uint8_t RAIN_SENSOR_PIN = 34;
const uint8_t FLOW_SENSOR_PIN = 35; // optional

// ─── GLOBALS ───────────────────────────────────────────────────────────────

struct ZoneConfig {
  char name[24];
  bool enabled;
  uint8_t days;       // bitmask: 0=Sun,1=Mon...6=Sat
  uint8_t startHour;
  uint8_t startMinute;
  uint16_t duration;  // seconds
};

struct Config {
  ZoneConfig zones[ZONE_COUNT];
  bool rainOverride;
  uint8_t mqttEnabled;
  char mqttServer[40];
  uint16_t mqttPort;
  char mqttUser[32];
  char mqttPass[32];
  char mqttTopic[32];
  bool flowEnabled;
  char wifiSsid[33];
  char wifiPass[65];
};

Config config;
Preferences prefs;

AsyncWebServer server(80);
AsyncEventSource events("/events");

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool zoneActive[ZONE_COUNT] = { false };
unsigned long zoneStarted[ZONE_COUNT] = { 0 };
bool rainDetected = false;
unsigned long lastMqttReconnect = 0;
unsigned long lastNtpSync = 0;
bool timeSynced = false;

// ─── DEFAULTS ──────────────────────────────────────────────────────────────

const Config DEFAULT_CONFIG = {
  {
    { "Lewa", true,  0b0111111, 6, 0, 600 },
    { "Prawa", true, 0b0111111, 6, 10, 600 },
    { "Kroplująca lewa", true,    0b0111111, 7, 0, 480 },
    { "Tył", true,            0b0010010, 7, 30, 300 },
    { "Przód", true,          0b0010010, 8, 0, 300 },
    { "Rabaty", true, 0b0111111, 20, 0, 900 },
  },
  true,
  0,
  "192.168.1.100",
  1883,
  "",
  "",
  "irrigation",
  false,
  "",
  "",
};

// ─── HELPERS ───────────────────────────────────────────────────────────────

void saveConfig() {
  prefs.begin("irrig", false);
  prefs.putBytes("config", &config, sizeof(config));
  prefs.end();
}

void loadConfig() {
  prefs.begin("irrig", false);
  size_t len = prefs.getBytes("config", &config, sizeof(config));
  if (len != sizeof(config)) {
    memcpy(&config, &DEFAULT_CONFIG, sizeof(config));
    saveConfig();
  }
  prefs.end();
}

void setZone(uint8_t z, bool on) {
  if (z >= ZONE_COUNT) return;
  digitalWrite(RELAY_PINS[z], on ? RELAY_ON : RELAY_OFF);
  zoneActive[z] = on;
  if (on) zoneStarted[z] = millis();
  else zoneStarted[z] = 0;
}

void allZonesOff() {
  for (uint8_t i = 0; i < ZONE_COUNT; i++) setZone(i, false);
}

void checkRainSensor() {
  bool raining = digitalRead(RAIN_SENSOR_PIN) == LOW;
  if (raining != rainDetected) {
    rainDetected = raining;
    if (raining && config.rainOverride) allZonesOff();
  }
}

bool isDayMatch(uint8_t dayMask) {
  time_t now = time(nullptr);
  struct tm* tm = localtime(&now);
  return (dayMask >> tm->tm_wday) & 1;
}

void printTime() {
  time_t now = time(nullptr);
  struct tm* tm = localtime(&now);
  char buf[20];
  strftime(buf, sizeof(buf), "%H:%M %d-%m-%Y", tm);
  Serial.printf("  Czas: %s\n", buf);
}

// ─── SCHEDULE ENGINE ──────────────────────────────────────────────────────

void checkSchedules() {
  if (!timeSynced) return;
  if (rainDetected && config.rainOverride) return;

  time_t now = time(nullptr);
  struct tm* tm = localtime(&now);
  uint8_t hour = tm->tm_hour;
  uint8_t minute = tm->tm_min;

  // check if any active zone
  bool anyActive = false;
  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    if (zoneActive[i]) anyActive = true;
  }

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    ZoneConfig* z = &config.zones[i];
    if (!z->enabled) continue;
    if (!isDayMatch(z->days)) continue;

    if (!anyActive) {
      if (hour == z->startHour && minute == z->startMinute) {
        setZone(i, true);
        Serial.printf("  START strefa %d (%s) czas: %d min\n", i, z->name, z->duration / 60);
        return; // one zone at a time
      }
    }

    // stop zone after duration
    if (zoneActive[i]) {
      unsigned long elapsed = (millis() - zoneStarted[i]) / 1000;
      if (elapsed >= z->duration) {
        setZone(i, false);
        Serial.printf("  STOP strefa %d (%s)\n", i, z->name);
      }
    }
  }
}

// ─── MQTT ──────────────────────────────────────────────────────────────────

void mqttCallback(char* topic, byte* payload, unsigned int len) {
  char buf[64];
  memcpy(buf, payload, min(len, (unsigned)63));
  buf[min(len, (unsigned)63)] = 0;

  // topic: irrigation/zone/0/command
  int zone = -1;
  if (sscanf(topic, "%*s/zone/%d/command", &zone) == 1) {
    if (zone >= 0 && zone < ZONE_COUNT) {
      if (strcmp(buf, "ON") == 0) {
        setZone(zone, true);
        zoneStarted[zone] = millis();
      } else if (strcmp(buf, "OFF") == 0) {
        setZone(zone, false);
      }
    }
  }
}

void reconnectMQTT() {
  if (!config.mqttEnabled) return;
  if (strlen(config.mqttServer) < 7) return;
  if (millis() - lastMqttReconnect < 30000) return;
  lastMqttReconnect = millis();

  Serial.printf("MQTT connecting to %s...\n", config.mqttServer);
  mqtt.setServer(config.mqttServer, config.mqttPort);
  mqtt.setCallback(mqttCallback);

  char clientId[32];
  snprintf(clientId, 32, "esp32-irrigation-%06x", (uint32_t)(ESP.getEfuseMac() >> 24));

  bool ok = false;
  if (strlen(config.mqttUser) > 0) {
    ok = mqtt.connect(clientId, config.mqttUser, config.mqttPass);
  } else {
    ok = mqtt.connect(clientId);
  }

  if (ok) {
    Serial.println("  MQTT OK");
    char topic[64];
    for (int i = 0; i < ZONE_COUNT; i++) {
      snprintf(topic, 64, "%s/zone/%d/command", config.mqttTopic, i);
      mqtt.subscribe(topic);
    }
    mqtt.subscribe("irrigation/all/command");
  } else {
    Serial.printf("  MQTT fail rc=%d\n", mqtt.state());
  }
}

void publishMQTT() {
  if (!mqtt.connected()) return;
  char topic[64];
  char buf[16];

  for (int i = 0; i < ZONE_COUNT; i++) {
    snprintf(topic, 64, "%s/zone/%d/status", config.mqttTopic, i);
    strcpy(buf, zoneActive[i] ? "ON" : "OFF");
    mqtt.publish(topic, buf, true);

    snprintf(topic, 64, "%s/zone/%d/duration", config.mqttTopic, i);
    itoa(config.zones[i].duration, buf, 10);
    mqtt.publish(topic, buf, true);
  }

  mqtt.publish("irrigation/rain", rainDetected ? "RAIN" : "DRY", true);
}

// ─── WEB HANDLERS ──────────────────────────────────────────────────────────

void sendZoneStatus(AsyncResponseStream* response) {
  response->print("[");
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (i > 0) response->print(",");
    response->printf(
      "{\"id\":%d,\"name\":\"%s\",\"active\":%s,\"remaining\":%d,\"enabled\":%s}",
      i, config.zones[i].name,
      zoneActive[i] ? "true" : "false",
      zoneActive[i] ? (config.zones[i].duration - (millis() - zoneStarted[i]) / 1000) : 0,
      config.zones[i].enabled ? "true" : "false"
    );
  }
  response->print("]");
}

void sendScheduleConfig(AsyncResponseStream* response) {
  response->print("[");
  for (int i = 0; i < ZONE_COUNT; i++) {
    if (i > 0) response->print(",");
    ZoneConfig* z = &config.zones[i];
    response->printf(
      "{\"id\":%d,\"name\":\"%s\",\"enabled\":%s,\"days\":%d,\"startHour\":%d,\"startMinute\":%d,\"duration\":%d}",
      i, z->name, z->enabled ? "true" : "false",
      z->days, z->startHour, z->startMinute, z->duration
    );
  }
  response->print("]");
}

void handleZoneCommand(AsyncWebServerRequest* req, uint8_t zone) {
  if (req->hasParam("action")) {
    String action = req->getParam("action")->value();
    if (action == "on") {
      setZone(zone, true);
      zoneStarted[zone] = millis();
    } else if (action == "off") {
      setZone(zone, false);
    } else if (action == "run" && req->hasParam("seconds")) {
      int sec = req->getParam("seconds")->value().toInt();
      if (sec > 0 && sec < 3600) {
        config.zones[zone].duration = sec;
        setZone(zone, true);
        zoneStarted[zone] = millis();
      }
    }
  }
  AsyncResponseStream* r = req->beginResponseStream("application/json");
  sendZoneStatus(r);
  req->send(r);
}

// ─── WEB SETUP ─────────────────────────────────────────────────────────────

void setupWebServer() {
  server.on("/api/zones", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncResponseStream* r = req->beginResponseStream("application/json");
    sendZoneStatus(r);
    req->send(r);
  });

  server.on("/api/schedule", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncResponseStream* r = req->beginResponseStream("application/json");
    sendScheduleConfig(r);
    req->send(r);
  });

  server.on("/api/rain", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncResponseStream* r = req->beginResponseStream("application/json");
    r->printf("{\"rain\":%s}", rainDetected ? "true" : "false");
    req->send(r);
  });

  server.on("/api/zone/0/command", HTTP_GET, [](AsyncWebServerRequest* req) { handleZoneCommand(req, 0); });
  server.on("/api/zone/1/command", HTTP_GET, [](AsyncWebServerRequest* req) { handleZoneCommand(req, 1); });
  server.on("/api/zone/2/command", HTTP_GET, [](AsyncWebServerRequest* req) { handleZoneCommand(req, 2); });
  server.on("/api/zone/3/command", HTTP_GET, [](AsyncWebServerRequest* req) { handleZoneCommand(req, 3); });
  server.on("/api/zone/4/command", HTTP_GET, [](AsyncWebServerRequest* req) { handleZoneCommand(req, 4); });
  server.on("/api/zone/5/command", HTTP_GET, [](AsyncWebServerRequest* req) { handleZoneCommand(req, 5); });

  server.on("/api/all/off", HTTP_GET, [](AsyncWebServerRequest* req) {
    allZonesOff();
    req->send(200, "application/json", "{\"ok\":true}");
  });

  server.on("/api/save", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      req->send(400, "application/json", "{\"error\":\"bad json\"}");
    },
    NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      String body((char*)data, len);
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);
      if (!err) {
        JsonArray arr = doc.as<JsonArray>();
        for (JsonVariant v : arr) {
          int id = v["id"];
          if (id >= 0 && id < ZONE_COUNT) {
            strncpy(config.zones[id].name, v["name"] | config.zones[id].name, 24);
            config.zones[id].enabled = v["enabled"];
            config.zones[id].days = v["days"];
            config.zones[id].startHour = v["startHour"];
            config.zones[id].startMinute = v["startMinute"];
            config.zones[id].duration = v["duration"];
          }
        }
        saveConfig();
        req->send(200, "application/json", "{\"ok\":true}");
      }
    }
  );

  server.on("/api/info", HTTP_GET, [](AsyncWebServerRequest* req) {
    AsyncResponseStream* r = req->beginResponseStream("application/json");
    time_t now = time(nullptr);
    struct tm* tm = localtime(&now);
    char timeBuf[32];
    strftime(timeBuf, sizeof(timeBuf), "%H:%M %d-%m-%Y", tm);
    r->printf(
      "{\"version\":\"%s\",\"time\":\"%s\",\"synced\":%s,\"uptime\":%lu,\"heap\":%u,\"mqtt\":%s,\"rain\":%s}",
      FIRMWARE_VERSION, timeBuf,
      timeSynced ? "true" : "false",
      millis() / 1000,
      ESP.getFreeHeap(),
      mqtt.connected() ? "true" : "false",
      rainDetected ? "true" : "false"
    );
    req->send(r);
  });

  server.on("/api/wifi", HTTP_POST,
    [](AsyncWebServerRequest* req) {
      req->send(400, "application/json", "{\"error\":\"bad json\"}");
    },
    NULL,
    [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
      String body((char*)data, len);
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, body);
      if (!err) {
        strncpy(config.wifiSsid, doc["ssid"] | "", 32);
        strncpy(config.wifiPass, doc["pass"] | "", 64);
        saveConfig();
        req->send(200, "application/json", "{\"ok\":true}");
        delay(1000);
        ESP.restart();
        return;
      }
    }
  );

  events.onConnect([](AsyncEventSourceClient* client) {
    client->send("ok", NULL, millis(), 1000);
  });

  server.addHandler(&events);

  // CORS
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  // OTA firmware update
  server.on("/api/ota", HTTP_POST,
    [](AsyncWebServerRequest *request) {
      request->send(200);
    },
    [](AsyncWebServerRequest *request, String filename, size_t index,
       uint8_t *data, size_t len, bool final) {
        if (!index) {
          Serial.printf("OTA Start: %s\n", filename.c_str());
          Update.begin(UPDATE_SIZE_UNKNOWN);
        }
        Update.write(data, len);
        if (final) {
          Serial.printf("OTA End: %u bytes\n", index + len);
          Update.end(true);
          ESP.restart();
        }
    }
  );

  server.begin();
}

// ─── SETUP ─────────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  Serial.printf("\n\n=== Irrigation Controller v%s ===\n", FIRMWARE_VERSION);

  for (uint8_t i = 0; i < ZONE_COUNT; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], RELAY_OFF);
  }
  pinMode(RAIN_SENSOR_PIN, INPUT_PULLUP);

  loadConfig();

  if (strlen(config.wifiSsid) > 0) {
    WiFi.begin(config.wifiSsid, config.wifiPass);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      attempts++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("  WiFi OK: %s (%s)\n", WiFi.localIP().toString().c_str(), WiFi.SSID().c_str());
    } else {
      Serial.println("  WiFi FAIL — uruchamiam AP...");
      WiFi.mode(WIFI_AP);
      WiFi.softAP("Nawodnienie-AP");
      Serial.printf("  AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    }
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Nawodnienie-AP");
    Serial.printf("  AP IP: %s\n", WiFi.softAPIP().toString().c_str());
  }

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  // NTP
  configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", ntpServer);
  time_t now = time(nullptr);
  int retries = 0;
  while (now < 100000 && retries < 20) {
    delay(500);
    now = time(nullptr);
    retries++;
  }
  timeSynced = (now > 100000);
  if (timeSynced) printTime();

  setupWebServer();

  if (config.mqttEnabled) {
    reconnectMQTT();
  }

  Serial.println("  System gotowy!");
}

// ─── LOOP ──────────────────────────────────────────────────────────────────

void loop() {
  checkRainSensor();

  if (timeSynced) {
    checkSchedules();
  }

  // sync NTP every hour
  if (millis() - lastNtpSync > 3600000) {
    lastNtpSync = millis();
    struct tm tm;
    if (getLocalTime(&tm)) timeSynced = true;
  }

  if (mqtt.connected()) {
    mqtt.loop();
    static unsigned long lastPub = 0;
    if (millis() - lastPub > 15000) {
      lastPub = millis();
      publishMQTT();
    }
  } else if (config.mqttEnabled) {
    reconnectMQTT();
  }

  // SSE events
  static unsigned long lastEvent = 0;
  if (millis() - lastEvent > 2000) {
    lastEvent = millis();
    String json = "{\"zones\":[";
    for (int i = 0; i < ZONE_COUNT; i++) {
      if (i > 0) json += ",";
      json += "{\"id\":" + String(i) + ",\"a\":" + (zoneActive[i] ? "true" : "false");
      if (zoneActive[i]) {
        int rem = config.zones[i].duration - (millis() - zoneStarted[i]) / 1000;
        if (rem < 0) rem = 0;
        json += ",\"r\":" + String(rem);
      }
      json += "}";
    }
    json += "],\"rain\":" + String(rainDetected ? "true" : "false") + "}";
    events.send(json.c_str(), "update", millis());
  }
}
