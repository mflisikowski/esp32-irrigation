# Dokumentacja Sprzętowa — System Nawadniania

## Spis treści

1. [Urządzenie 1: ESP32-C6 DevKit](#urzadzenie-1-esp32-c6-devkit)
2. [Urządzenie 2: Moduł Przekaźnika 8-Kanałowego](#urzadzenie-2-modul-przekaznika-8-kanalowego)
3. [Połączenie ESP32-C6 z Przekaźnikiem](#polaczenie-esp32-c6-z-przekaznikiem)
4. [Programowanie](#programowanie)
5. [Protokoły Komunikacyjne](#protokoly-komunikacyjne)
6. [Uwagi Techniczne](#uwagi-techniczne)

---

## Urządzenie 1: ESP32-C6 DevKit

### Źródło

- **Allegro:** https://allegro.pl/oferta/modul-esp32-c6-mikrokontroler-wifi-6-zigbee-thread-ble-5-0-do-arduino-usb-c-18545765691
- **Moduł:** ESP32-C6-WROOM-1

### Specyfikacja Techniczna

| Parametr | Wartość |
|---|---|
| **Procesor** | RISC-V 32-bit, jednordzeniowy |
| **Taktowanie** | 160 MHz (HP) + 20 MHz (LP) |
| **RAM** | 512 KB SRAM + 16 KB LP SRAM |
| **Flash** | 4–8 MB SPI |
| **GPIO** | 24 pinów (z 31 fizycznych) |
| **ADC** | 12-bit, 8 kanałów (ADC1: GPIO0–6, ADC2: GPIO9) |
| **Zasilanie** | 3.3V (nie jest 5V tolerant!) |
| **USB** | 2× USB-C (UART + natywne USB 2.0 FS) |

### łączność Bezprzewodowa

| Protokół | Standard | Uwagi |
|---|---|---|
| **Wi-Fi 6** | 802.11ax (2.4 GHz) | OFDMA, MU-MIMO, TWT (oszczędność energii) |
| **Bluetooth** | 5.0 LE + Mesh | Coded PHY, 2 Mbps throughput |
| **Zigbee** | 3.0 (IEEE 802.15.4) | Coordinator / Router / End Device |
| **Thread** | 1.3 (IEEE 802.15.4) | Matter over Thread |
| **Matter** | over Wi-Fi + Thread | Pełna kompatybilność ekosystemów |

### Pinout — Kluczowe Piny

| GPIO | Funkcja | Uwagi |
|---|---|---|
| GPIO0 | ADC1_CH0, Strapping | ⚠️ Nie używać jako output przy starcie |
| GPIO1 | ADC1_CH1 | |
| GPIO2 | ADC1_CH2, FSPIQ | |
| GPIO3 | ADC1_CH3 | |
| GPIO4 | ADC1_CH4, MTMS | ⚠️ Strapping pin (10k pull-up na custom PCB) |
| GPIO5 | ADC1_CH5, MTDI | ⚠️ Strapping pin |
| GPIO6 | ADC1_CH6, MTCK, LP_I2C_SDA | |
| GPIO7 | MTDO, LP_I2C_SCL | |
| GPIO8 | RGB LED (WS2812) | ⚠️ Strapping pin — musi być HIGH przy starcie |
| GPIO9 | BOOT button | ⚠️ Pull LOW = Download mode |
| GPIO10 | GPIO | Bezpieczny |
| GPIO11 | GPIO | Bezpieczny |
| GPIO12 | USB_D- | ⚠️ Dedykowany do USB-JTAG |
| GPIO13 | USB_D+ | ⚠️ Dedykowany do USB-JTAG |
| GPIO14 | GPIO | |
| GPIO15 | JTAG_SEL | ⚠️ Strapping pin |
| GPIO16 | U0TXD (UART TX) | Przez CH343 USB-UART |
| GPIO17 | U0RXD (UART RX) | Przez CH343 USB-UART |
| GPIO18 | SDIO_CMD | Połączony z flash |
| GPIO19 | SDIO_CLK | Połączony z flash |
| GPIO20 | SDIO_DATA0 | |
| GPIO21 | SDIO_DATA1 | |
| GPIO22 | SDIO_DATA2 | |
| GPIO23 | SDIO_DATA3 | |
| GPIO24–30 | SPI0/1 (flash) | ⚠️ NIE UŻYWAĆ — wewnętrzny flash |

### Bezpieczne Piny do Użycia (Rekomendowane)

**Najlepsze piny GPIO dla projektu nawadniania (unikanie strapping + flash):**

```
GPIO10, GPIO11, GPIO14, GPIO16, GPIO17, GPIO20, GPIO21, GPIO22, GPIO23
```

**Piny z ADC (do czujników):**

```
GPIO0, GPIO1, GPIO2, GPIO3, GPIO4, GPIO5, GPIO6, GPIO9
```

### Układ Zasilania

```
USB-C (5V) → LDO 3.3V → ESP32-C6 module
         ↓
      J5 jumper (pomiar prądu)
```

### Sekcja Programowania

| Funkcja | Pin | Opis |
|---|---|---|
| TXD0 | GPIO16 | Serial do PC |
| RXD0 | GPIO17 | Serial z PC |
| BOOT | GPIO9 | Pull LOW + Reset = Download mode |
| EN/RST | RST pin | Reset układu |

**Kroki flashowania:**
1. Podłącz kabel USB do portu **UART USB-C** (lewego)
2. Wybierz **ESP32C6 Dev Module** w Arduino/PlatformIO
3. Kliknij "Upload". Przytrzymaj **BOOT** jeśli flashowanie nie działa

### Sterowanie LED RGB (WS2812)

```cpp
#include <FastLED.h>

#define NUM_LEDS 1
#define DATA_PIN 8

CRGB leds[NUM_LEDS];

void setup() {
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
}

void loop() {
  leds[0] = CRGB::Red;   FastLED.show(); delay(1000);
  leds[0] = CRGB::Green; FastLED.show(); delay(1000);
  leds[0] = CRGB::Blue;  FastLED.show(); delay(1000);
}
```

### Strapping Pins — Tabela Logiczna

| GPIO9 (BOOT) | GPIO8 | Tryb | Opis |
|---|---|---|---|
| HIGH (puszczony) | HIGH | Normal Flash Boot | Domyślny — uruchamia program |
| LOW (przytrzymaj) | HIGH | ROM Serial Bootloader | Download mode — oczekuje na firmware |
| Inne kombinacje | — | Invalid | Unikać |

---

## Urządzenie 2: Moduł Przekaźnika 8-Kanałowego

### Źródło

- **Allegro:** https://allegro.pl/oferta/modul-8-kanalowy-5v-10a-przekaznik-sterowany-stanem-niskim-0v-do-arduino-11435635416
- **Typ:** Moduł 8-kanałowy, 5V, 10A, sterowany stanem niskim (LOW level trigger)

### Specyfikacja Techniczna

| Parametr | Wartość |
|---|---|
| **Liczba kanałów** | 8 |
| **Napięcie zasilania** | 5V DC |
| **Prąd roboczy** | 400 mA (cały moduł) |
| **Prąd wyzwalania** | 3–5 mA na kanał |
| **Napięcie wyzwalania LOW** | 0V – 1.5V (stan niski = aktywny) |
| **Obciążenie wyjściowe** | 10A @ 250VAC lub 10A @ 30DC |
| **Izolacja** | Optoizolatory (opcjonalnie odłączalne) |
| **Wymiary** | 138 × 56 × 19.3 mm |
| **Waga** | ~108 g |

### Wejścia (Strona Niskiego Napięcia)

| Pin | Opis |
|---|---|
| **VCC** | +5V zasilanie przekaźnika |
| **GND** | masa |
| **IN1–IN8** | sygnały sterujące (active LOW) |

### Wyjścia (Strona Wysokiego Napięcia) — każdy kanał

| Pin | Opis |
|---|---|
| **COM** | wspólny (common) |
| **NO** | normally open — otwarty domyślnie |
| **NC** | normally closed — zamknięty domyślnie |

### Zasada Działania

**Moduł jest sterowany stanem niskim (Active LOW):**

| Stan IN | Stan przekaźnika | Urządzenie |
|---|---|---|
| **LOW (0V)** | ⚡ ZWŁOŻONY (ON) | Włączone |
| **HIGH (3.3V/5V)** | 🔓 ROZŁOŻONY (OFF) | Wyłączone |

### Skoki Jumperów VCC/JD-VCC

Moduł ma opcjonalną izolację optyczną:

| Jumper VCC–JD-VCC | Zachowanie |
|---|---|
| **Założony (domyślnie)** | Przekaźnik i logika dzielą to samo VCC |
| **Zdjęty** | Osobne zasilanie dla przekaźników (JD-VCC) — lepsza izolacja |

### Punkt Styku: ESP32-C6 (3.3V) → Przekaźnik (5V)

**WAŻNE:** ESP32-C6 ma GPIO na 3.3V, a moduł przekaźnika wymaga 5V do zasilania cewek.

**Ale:** Przekaźniki z optoizolatorami i active LOW trigger działają z 3.3V na wejściu logicznym. Prąd wyzwalania 3–5 mA jest osiągalny z GPIO ESP32-C6 (max ~12 mA sink).

**Rozwiązanie:**

```
ESP32 GPIO (3.3V) ────→ IN1–IN8 (przekaźnika)
                          ↑
ESP32 5V (z USB) ────→ VCC (przekaźnika)
ESP32 GND ────────────→ GND (przekaźnika)
```

> **Nie trzeba level shiftera** dla modułów z optoizolatorami i active LOW trigger.
> Sprawdź na swoim egzemplarzu — jeśli 3.3V nie załącza przekaźnika, dodaj prosty transistor (NPN np. BC547) lub level shifter.

---

## Połączenie ESP32-C6 z Przekaźnikiem

### Schemat Połączenia

```
┌─────────────────┐          ┌─────────────────────┐
│   ESP32-C6      │          │   8-CH Relay Module  │
│   DevKit        │          │                      │
│                 │          │                      │
│  GPIO10 ────────┼──── IN1  │  Kanał 1             │
│  GPIO11 ────────┼──── IN2  │  Kanał 2             │
│  GPIO14 ────────┼──── IN3  │  Kanał 3             │
│  GPIO20 ────────┼──── IN4  │  Kanał 4             │
│  GPIO21 ────────┼──── IN5  │  Kanał 5             │
│  GPIO22 ────────┼──── IN6  │  Kanał 6             │
│  GPIO23 ────────┼──── IN7  │  Kanał 7             │
│  GPIO16 ────────┼──── IN8  │  Kanał 8             │
│                 │          │                      │
│  5V  ───────────┼──── VCC  │  Zasilanie 5V        │
│  GND ───────────┼──── GND  │  Masa                │
└─────────────────┘          └─────────────────────┘
```

### Schemat Połączenia (alternatywne piny)

Jeśli potrzebujesz GPIO16/17 do UART, użyj innych pinów:

```
IN1 → GPIO10
IN2 → GPIO11
IN3 → GPIO14
IN4 → GPIO20
IN5 → GPIO21
IN6 → GPIO22
IN7 → GPIO23
IN8 → GPIO9  (uważaj — strapping pin, ale działa jako output po starcie)
```

### Zasilanie

- **ESP32-C6:** Zasilany z USB-C (5V → 3.3V LDO)
- **Przekaźnik:** Zasilany z 5V ESP32 (pin 5V) lub osobnego zasilacza 5V
- **Obciążenie przekaźnika:** Osobny obwód (230V AC / 30V DC)

**UWAGA:** Przy 8 aktywnych przekaźnikach jednocześnie moduł pobiera ~400mA na 5V. Upewnij się, że zasilacz USB/ESP32 to wytrzyma. Lepiej użyć **osobnego zasilacza 5V** dla przekaźnika.

---

## Programowanie

### Środowisko Programistyczne

| Opcja | Link | Uwagi |
|---|---|---|
| **Arduino IDE** | Board Manager: "esp32" by Espressif | Wybierz "ESP32C6 Dev Module" |
| **PlatformIO** | `board = esp32-c6-devkitc-1` | Najlepsze dla projektów wieloplatformowych |
| **ESP-IDF** | docs.espressif.com | Oficjalny SDK — najwięcej możliwości |
| **ESPHome** | esphome.io | Dla Home Assistant — zero kodowania |

### Arduino IDE — Konfiguracja

```
Tools → Board: ESP32C6 Dev Module
Tools → CPU Frequency: 160 MHz
Tools → Flash Size: 4MB (lub 8MB)
Tools → Flash Mode: QIO
Tools → Upload Speed: 921600
Tools → Partition Scheme: Default 4MB
```

### Przykładowy Kod — Sterowanie 8 Przekaźnikami

```cpp
// Definicja pinów ESP32-C6 → IN1–IN8
const int RELAY_PINS[] = {10, 11, 14, 20, 21, 22, 23, 16};
const int NUM_RELAYS = 8;

void setup() {
  Serial.begin(115200);

  // Inicjalizacja pinów przekaźników
  for (int i = 0; i < NUM_RELAYS; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], HIGH); // HIGH = OFF (active LOW)
  }

  Serial.println("ESP32-C6 + 8-CH Relay ready!");
}

void loop() {
  // Przykład: sekwencyjne włączanie/wyłączanie
  for (int i = 0; i < NUM_RELAYS; i++) {
    digitalWrite(RELAY_PINS[i], LOW);  // LOW = ON
    delay(1000);
    digitalWrite(RELAY_PINS[i], HIGH); // HIGH = OFF
    delay(500);
  }
}

// Funkcja pomocnicza
void setRelay(int channel, bool state) {
  if (channel >= 0 && channel < NUM_RELAYS) {
    // state = true → ON, state = false → OFF
    // Active LOW: ON = LOW, OFF = HIGH
    digitalWrite(RELAY_PINS[channel], state ? LOW : HIGH);
  }
}
```

### Przykładowy Kod — Sterowanie z Telefonu (Wi-Fi)

```cpp
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "TWOJ_SSID";
const char* password = "TWOJE_HASLO";

const int RELAY_PINS[] = {10, 11, 14, 20, 21, 22, 23, 16};
bool relayStates[8] = {false};

WebServer server(80);

void handleRoot() {
  String html = "<h1>ESP32-C6 Irrigation Control</h1>";
  for (int i = 0; i < 8; i++) {
    html += "<p>Kanal " + String(i+1) + ": ";
    html += "<a href='/toggle/" + String(i) + "'>";
    html += relayStates[i] ? "WYLACZ" : "WLACZ";
    html += "</a></p>";
  }
  server.send(200, "text/html", html);
}

void handleToggle() {
  int channel = server.pathArg(0).toInt();
  if (channel >= 0 && channel < 8) {
    relayStates[channel] = !relayStates[channel];
    digitalWrite(RELAY_PINS[channel], relayStates[channel] ? LOW : HIGH);
  }
  server.sendHeader("Location", "/");
  server.send(302);
}

void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 8; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    digitalWrite(RELAY_PINS[i], HIGH);
  }

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/toggle/", HTTP_GET, handleToggle);
  server.begin();
}

void loop() {
  server.handleClient();
}
```

---

## Protokoły Komunikacyjne

### Zigbee (ESP32-C6 jako Router/End Device)

ESP32-C6 może działać jako:

- **Zigbee Coordinator** (z ESP-Matter SDK + ESP-IDF)
- **Zigbee Router** (przekaźnik sygnału)
- **Zigbee End Device** (czujnik/sensor)

**Wymagania:**
- ESP-IDF v5.1+ z ESP-Zigbee SDK
- Zigbee coordinator w sieci (np. Zigbee2MQTT, ZHA)
- Konfiguracja jako HA (Home Automation) device

**Obsługiwane klustery ZCL:**
- On/Off (0x0006) — sterowanie przekaźnikami ✅
- Level Control (0x0008)
- Temperature Measurement (0x0402)
- Humidity Measurement (0x0405)
- IAS Zone (0x0500) — czujniki ruchu/otwarcia

### Thread / Matter

ESP32-C6 może tworzyć urządzenia Matter:

- **Matter over Thread** (niski pobór energii)
- **Matter over Wi-Fi** (prostsza konfiguracja)

**Przykłady urządzeń Matter:**
- `MatterOnOffPlugin` — przekaźnik ON/OFF ✅
- `MatterDimmablePlugin` — przekaźnik z ściemnianiem
- `MatterTemperatureSensor` — czujnik temperatury
- `MatterHumiditySensor` — czujnik wilgotności

**Wymagania:**
- ESP-Matter SDK lub Arduino Matter Library
- Matter Controller (Apple Home, Google Home, Home Assistant)
- Thread Border Router (jeśli Thread)

### ESPHome (dla Home Assistant)

```yaml
# esp32-c6-irrigation.yaml
esphome:
  name: irrigation-controller

esp32:
  board: esp32-c6-devkitc-1
  framework:
    type: arduino

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

api:
  password: !secret api_password

ota:
  password: !secret ota_password

# GPIO outputs dla przekaźników
output:
  - platform: gpio
    pin: GPIO10
    id: relay_1
  - platform: gpio
    pin: GPIO11
    id: relay_2
  - platform: gpio
    pin: GPIO14
    id: relay_3
  - platform: gpio
    pin: GPIO20
    id: relay_4
  - platform: gpio
    pin: GPIO21
    id: relay_5
  - platform: gpio
    pin: GPIO22
    id: relay_6
  - platform: gpio
    pin: GPIO23
    id: relay_7
  - platform: gpio
    pin: GPIO16
    id: relay_8

switch:
  - platform: output
    output: relay_1
    name: "Strefa 1 Nawadniania"
  - platform: output
    output: relay_2
    name: "Strefa 2 Nawadniania"
  - platform: output
    output: relay_3
    name: "Strefa 3 Nawadniania"
  - platform: output
    output: relay_4
    name: "Strefa 4 Nawadniania"
  - platform: output
    output: relay_5
    name: "Strefa 5 Nawadniania"
  - platform: output
    output: relay_6
    name: "Strefa 6 Nawadniania"
  - platform: output
    output: relay_7
    name: "Strefa 7 Nawadniania"
  - platform: output
    output: relay_8
    name: "Strefa 8 Nawadniania"
```

---

## Uwagi Techniczne

### ⚠️ Bezpieczeństwo

1. **Napięcie sieciowe 230V** — przekaźniki przełączają wysokie napięcie. **Zachowaj ostrożność!**
2. **Oddziel zasilanie** — nie zasilaj przekaźnika z pinu 5V ESP32 przy pełnym obciążeniu (8 × 50mA = 400mA). Użyj oddzielnego zasilacza 5V/2A+.
3. **Optoizolatory** — jeśli chcesz pełnej izolacji galwanicznej, odłącz jumper VCC-JD-VCC i podaj osobne 5V do JD-VCC.
4. **Obciążenie** — max 10A na kanał. Dla pompy nawadnianiowej (zazwyczaj 0.5–2A) to wystarczający zapas.

### ⚠️ Strapping Pins ESP32-C6

| Pin | Zachowanie przy starcie | Rekomendacja |
|---|---|---|
| GPIO0 | Boot mode | Unikaj jako output |
| GPIO4 | Strapping | 10k pull-up na custom PCB |
| GPIO5 | Strapping | 10k pull-up na custom PCB |
| GPIO8 | MUST be HIGH | Nie obciążaj przy starcie |
| GPIO9 | BOOT button | Pull LOW = download mode |
| GPIO15 | JTAG_SEL | Unikaj |

### ⚠️ Równoczesne użycie Wi-Fi + Zigbee/Thread

ESP32-C6 ma **jedną ścieżkę RF** — Wi-Fi i Thread/Nie mogą działać jednocześnie z pełną wydajnością.

**Rekomendacje:**
- Używaj **Wi-Fi** do komunikacji z serwerem/telefonem
- Używaj **Zigbee** lub **Thread** do komunikacji z sensorami
- Nie łącz obu protokołów jednocześnie w trybie ciągłym
- Dla OTA updates: włączaj Wi-Fi tymczasowo

### Zalecane Zasilanie dla Systemu Nawadniania

```
Zasilacz 5V/3A (osobny)
  ├──→ Przekaźnik VCC (5V)
  └──→ ESP32-C6 USB-C (5V)
         └──→ 3.3V LDO → ESP32-C6

Obwód 230V AC:
  Przekaźnik COM/NO → Pompa nawadniania
  Przekaźnik COM/NO → Zawór 1
  Przekaźnik COM/NO → Zawór 2
  ...
```

### Dokumentacja Oficjalna

| Dokument | Link |
|---|---|
| ESP32-C6 Datasheet | https://www.espressif.com/sites/default/files/documentation/esp32-c6_datasheet_en.pdf |
| ESP32-C6 Technical Reference Manual | https://www.espressif.com/sites/default/files/documentation/esp32-c6_technical_reference_manual_en.pdf |
| ESP32-C6-WROOM-1 Datasheet | https://www.espressif.com/sites/default/files/documentation/esp32-c6-wroom-1_wroom-1u_datasheet_en.pdf |
| DevKitC-1 v1.2 User Guide | https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32c6/esp32-c6-devkitc-1/user_guide.html |
| ESP-IDF Get Started | https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/get-started/index.html |
| Arduino ESP32 Matter Docs | https://docs.espressif.com/projects/arduino-esp32/en/latest/matter/matter.html |
| ESP Zigbee SDK | https://docs.espressif.com/projects/esp-zigbee-sdk/en/latest/esp32c6/introduction.html |
| ESP Home Zigbee Component | https://esphome.io/components/zigbee |

---

## Podsumowanie Dla Agenta Dokumentacji

**Zakupione urządzenia:**

1. **ESP32-C6 DevKit (USB-C)** — mikrokontroler RISC-V 160MHz z Wi-Fi 6, BLE 5, Zigbee 3.0, Thread 1.3, Matter
2. **Moduł Przekaźnika 8-Kanałowego (5V, 10A, Active LOW)** — optoizolatory, sterowanie 3.3V/5V

**Kompatybilność:**
- ESP32-C6 GPIO (3.3V) → Przekaźnik IN (Active LOW trigger) — **kompatybilne bez level shiftera**
- ESP32-C6 5V pin → Przekaźnik VCC — **wymaga zewnętrznego zasilacza przy pełnym obciążeniu**

**Zastosowanie w projekcie nawadniania:**
- 8 niezależnych stref nawadniania
- Sterowanie przez Wi-Fi (aplikacja webowa, MQTT, Home Assistant)
- Sterowanie przez Zigbee/Thread (Matter, Zigbee2MQTT)
- Czujniki ADC (wilgotność gleby, temperatura) na GPIO0–6

**Kluczowe uwagi dla implementacji:**
- Active LOW: LOW = ON, HIGH = OFF
- Bezpieczne piny GPIO: 10, 11, 14, 20, 21, 22, 23
- Osobne zasilanie 5V dla przekaźnika zalecane
- Unikaj użycia GPIO24–30 (wewnętrzny flash)
- GPIO8 = RGB LED (nie obciążać przy starcie)
- GPIO9 = BOOT (nie obciążać nisko przy starcie)
