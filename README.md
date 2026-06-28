# System Nawodnienia Ogrodu — Dokumentacja Projektu

## Dla kogo jest ten dokument?

- **Laik**: dowiesz się czym jest ten projekt i jak go uruchomić
- **Techniczny**: zrozumiesz architekturę, API i konfigurację

---

## Co to jest?

Automatyczny system nawadniania ogrodu sterowany mikrokontrolerem **ESP32-C6**. Firmware pisany w czystym C na ESP-IDF v5.3+ (nie Arduino). Panel webowy w React 19 + TypeScript, budowany przez Vite do pamięci SPIFFS na ESP32.

### Co kupiłeś?

1. **ESP32-C6 DevKit (USB-C)** — mały komputer z WiFi 6, Bluetooth 5, Zigbee 3.0, Thread 1.3
2. **Moduł przekaźnika 8-Kanałowego** — 5V, 10A, Active LOW, z optoizolatorami

Te dwa urządzenia tworzą sterownik nawadniania. ESP32 steruje przekaźnikami, które otwierają/zamykają zawory wodne.

### Dla laika: wyobraź sobie...

Masz ogród z 6 strefami trawnika i rabatami. Zamiast ręcznie otwierać zawory, system:

1. Sprawdza pogodę (czujnik deszczu)
2. Automatycznie podlewa o ustawionych porach
3. Pozwala sterować przez telefon lub przeglądarkę
4. Wysyła status do systemu smart home (MQTT)

### Jak to działa?

Mały komputer (ESP32-C6) jest podłączony do 6 zaworów elektromagnetycznych przez przekaźniki. Na podstawie zaplanowanego harmonogramu otwiera i zamyka zawory. Czujnik deszczu wyłącza podlewanie gdy pada. Wszystkim sterujesz z poziomu telefonu przez przeglądarkę.

---

## Architektura

### Firmware (ESP-IDF, C)

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32-C6 (Firmware)                   │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │
│  │ WiFi Mgr │  │ Web Srv  │  │ MQTT     │  │ Zones    │ │
│  │ (AP/STA) │  │(HTTP+SSE)│  │ Client   │  │ (GPIO)   │ │
│  └──────────┘  └──────────┘  └──────────┘  └──────────┘ │
│       │              │              │              │      │
│       └──────────────┴──────────────┴──────────────┘      │
│                          │                               │
│                    ┌─────┴─────┐                         │
│                    │ FreeRTOS  │                         │
│                    │  Loop     │                         │
│                    └───────────┘                         │
└─────────────────────────────────────────────────────────┘
```

### Struktura plików

```
irrigation/
├── main/                    # Firmware ESP-IDF (C)
│   ├── app_main.c          # Punkt wejścia, inicjalizacja, FreeRTOS task loop
│   ├── app_config.c/h      # Konfiguracja NVS (namespace "irrig", key "config")
│   ├── zones.c/h           # Sterowanie zaworami (GPIO), harmonogram, czujnik deszczu
│   ├── wifi_mgr.c/h        # WiFi AP + STA, mDNS (irrigation.local)
│   ├── web_server.c/h      # HTTP API + SSE (Server-Sent Events)
│   ├── mqtt_client.c/h     # MQTT client (esp-mqtt)
│   └── time_sync.c/h       # NTP sync (SNTP, pool.ntp.org, CET/CEST)
├── web/                     # Panel webowy (React 19 + TypeScript + Vite)
│   └── src/
│       ├── App.tsx         # Główny komponent
│       ├── api/esp32.ts    # Klient HTTP (axios)
│       ├── components/     # Header, ZoneGrid, ZoneCard, ScheduleTable, StatusBar, OtaUpload
│       └── types/          # TypeScript types
├── data/                    # Pliki statyczne (SPIFFS image)
├── partitions.csv           # Tablica partycji (4MB Flash)
├── Makefile                 # Skróty make
└── CMakeLists.txt           # Build system ESP-IDF + web UI pipeline
```

---

## Szybki start (dla laika)

### Co potrzebujesz?

1. **ESP32-C6** (płytka deweloperska z USB-C)
2. **Moduł przekaźnika 8-Kanałowego** (5V, Active LOW)
3. **Kabel USB-C** do podłączenia i wgrania firmware
4. **Komputer z macOS/Linux/Windows**
5. **Zasilacz 5V/2A+** (zalecany dla przekaźnika)

### Krok po kroku

```bash
# 1. Zainstaluj ESP-IDF (jednorazowo)
#    Podążaj za: https://docs.espressif.com/projects/esp-idf/en/v5.3/esp32c6/get-started/index.html
. $HOME/esp/esp-idf/export.sh

# 2. Zainstaluj zależności web UI (jednorazowo)
make web-install

# 3. Ustaw target na ESP32-C6
make target-c6

# 4. Zbuduj i wgraj firmware
make run

# 5. Otwórz przeglądarkę
#    Połącz się z WiFi "Nawodnienie-AP" (otwarte, bez hasła)
#    Otwórz http://192.168.4.1
#    Skonfiguruj swoje WiFi
#    Po restarcie: http://irrigation.local (lub IP z logów)
```

### Gotowe!

Masz panel webowy do sterowania nawadnianiem.

---

## Szybki start (dla technicznego)

### Wymagania

| Komponent | Wersja | Uwagi |
|-----------|--------|-------|
| ESP-IDF | v5.3+ | `~/esp/esp-idf` |
| Node.js | 18+ | Do budowania web UI |
| Python | 3.8+ | Wymagane przez ESP-IDF |
| ESP32-C6 DevKit | USB-C | RISC-V 160MHz, WiFi 6 |
| Moduł przekaźnika | 8CH, 5V, 10A | Active LOW, optoizolatory |
| Zasilacz | 5V/2A+ | Osobny dla przekaźnika (zalecany) |

### Budowanie

```bash
. $HOME/esp/esp-idf/export.sh

# Pierwszy raz — zainstaluj zależności web UI
make web-install

# Zbuduj wszystko (firmware + web UI + SPIFFS image)
make build
```

Build automatycznie uruchamia `npm run build` w `web/`, kopiuje wynik do `data/`, a `idf.py` pakuje `data/` do obrazu SPIFFS.

### Wgrywanie

```bash
# Przez USB (pierwsze wgranie lub codzienne)
make run

# Przez sieć (OTA)
make ota

# Z指定 portem
make run PORT=/dev/cu.usbserial-20
```

### Logi

```bash
make monitor  # Ctrl-] aby wyjść
```

---

## Strefy nawadniania

| # | Nazwa | Powierzchnia | Typ zraszaczy | Zraszacze |
|---|-------|-------------|---------------|-----------|
| Z0 | Lewa | ~55 m² | Rotacyjne | 2× Rain Bird 5000 PC |
| Z1 | Prawa | ~55 m² | Rotacyjne | 2× Rain Bird 5000 PC |
| Z2 | Kroplująca lewa | ~43 m² | Statyczne | 4× Rain Bird 1800 + MP Rotator |
| Z3 | Tył | ~25 m² | Wąskostrumieniowe | 3× Rain Bird 1800 |
| Z4 | Przód | ~27 m² | Statyczne | 3× Rain Bird 1800 |
| Z5 | Rabaty | ~45 m.b. | Kroplujące | Linia PE16 + reduktor |

### Domyślny harmonogram (z kodu, `app_config.c`)

| # | Nazwa | Dni | Godzina startu | Czas podlewania |
|---|-------|-----|----------------|-----------------|
| Z0 | Lewa | Pon-Sob (0x3F) | 6:00 | 600 s (10 min) |
| Z1 | Prawa | Pon-Sob (0x3F) | 6:10 | 600 s (10 min) |
| Z2 | Kroplująca lewa | Pon-Sob (0x3F) | 7:00 | 480 s (8 min) |
| Z3 | Tył | Wt+Pt (0x12) | 7:30 | 300 s (5 min) |
| Z4 | Przód | Wt+Pt (0x12) | 8:00 | 300 s (5 min) |
| Z5 | Rabaty | Pon-Sob (0x3F) | 20:00 | 900 s (15 min) |

### Bitmaska dni tygodnia

| Bit | 0 | 1 | 2 | 3 | 4 | 5 | 6 |
|-----|---|---|---|---|---|---|---|
| Dzień | Nd | Pn | Wt | Sr | Cz | Pt | Sob |

Przykłady: `0x3F` = 0111111 = Pon-Sob. `0x12` = 010010 = Wt+Pt.

---

## Konfiguracja firmware

Konfiguracja przechowywana w NVS (namespace `"irrig"`, key `"config"`) jako blob. Wartości domyślne z `app_config.c`:

```c
static const app_config_t DEFAULT_CONFIG = {
    .zones = {
        { "Lewa",            true, 0x3F,  6,  0, 600 },
        { "Prawa",           true, 0x3F,  6, 10, 600 },
        { "Kroplująca lewa", true, 0x3F,  7,  0, 480 },
        { "Tył",             true, 0x12,  7, 30, 300 },
        { "Przód",           true, 0x12,  8,  0, 300 },
        { "Rabaty",          true, 0x3F, 20,  0, 900 },
    },
    .rainOverride = true,    // automatyczne wyłączanie przy deszczu
    .mqttEnabled  = 0,       // MQTT domyślnie wyłączony
    .mqttServer   = "192.168.1.100",
    .mqttPort     = 1883,
    .mqttUser     = "",
    .mqttPass     = "",
    .mqttTopic    = "irrigation",
    .flowEnabled  = false,
    .wifiSsid     = "",
    .wifiPass     = "",
};
```

---

## API HTTP

Wszystkie endpointy z `web_server.c`. CORS otwarty (`*`) na każdej odpowiedzi.

### Odczyt (GET)

| Ścieżka | Opis | Przykład |
|---------|------|----------|
| `/` | Panel webowy (static z SPIFFS) | `http://irrigation.local/` |
| `/api/zones` | Status stref (JSON) | `curl http://irrigation.local/api/zones` |
| `/api/schedule` | Harmonogram (JSON) | `curl http://irrigation.local/api/schedule` |
| `/api/rain` | Status deszczu | `curl http://irrigation.local/api/rain` |
| `/api/info` | System info (wersja, czas, WiFi, MQTT, heap) | `curl http://irrigation.local/api/info` |
| `/api/zone/{id}/command?action=on` | Włącz strefę | `curl "...?action=on"` |
| `/api/zone/{id}/command?action=off` | Wyłącz strefę | `curl "...?action=off"` |
| `/api/zone/{id}/command?action=run&seconds=300` | Uruchom na N sekund (max 3599) | `curl "...?action=run&seconds=300"` |
| `/api/all/off` | Wyłącz wszystkie strefy | `curl http://irrigation.local/api/all/off` |

### Zapis (POST)

| Ścieżka | Body | Opis |
|---------|------|------|
| `/api/save` | JSON array harmonogramu | Zapisz harmonogram do NVS |
| `/api/wifi` | `{"ssid":"...","pass":"..."}` | Zmiana WiFi + restart |
| `/api/ota` | Surowy `.bin` (octet-stream) | Aktualizacja firmware |

### SSE (Server-Sent Events)

- Endpoint: `GET /events`
- Interwał: ~2 sekundy
- Format: `event: update\ndata: {"zones":[{"id":0,"a":true,"r":120},...],"rain":false}\n\n`
- Max 6 jednoczesnych klientów

### Struktura odpowiedzi JSON

`/api/info`:
```json
{
  "version": "1.0.0",
  "time": "14:30 27-06-2026",
  "synced": true,
  "uptime": 3600,
  "heap": 120000,
  "mqtt": false,
  "rain": false,
  "wifiMode": "STA",
  "ip": "192.168.1.50",
  "ssid": "DomWiFi"
}
```

`/api/zones`:
```json
[
  {"id":0,"name":"Lewa","active":false,"remaining":0,"enabled":true},
  {"id":1,"name":"Prawa","active":true,"remaining":120,"enabled":true}
]
```

---

## MQTT

Domyślny prefiks topików: `irrigation` (konfigurowalne w `mqttTopic`).

### Subskrypcje (odczyt)

| Topic | Payload | Opis |
|-------|---------|------|
| `<topic>/zone/{id}/command` | `ON` / `OFF` | Sterowanie strefą |
| `<topic>/all/command` | `OFF` | Wyłącz wszystkie strefy |

### Publikacje (zapis, retained)

| Topic | Payload | Opis |
|-------|---------|------|
| `<topic>/zone/{id}/status` | `ON` / `OFF` | Stan strefy (co ~15 s) |
| `<topic>/zone/{id}/duration` | `300` | Czas podlewania w sekundach |
| `<topic>/rain` | `RAIN` / `DRY` | Stan czujnika deszczu |

### Konfiguracja

W `app_config.c` lub przez panel webowy:
- `mqttServer` — adres brokera MQTT
- `mqttPort` — port (domyślnie 1883)
- `mqttUser` / `mqttPass` — dane logowania (opcjonalnie)
- `mqttTopic` — prefiks topików (domyślnie `irrigation`)

Uwaga: brak auto-discovery (np. Home Assistant). Urządzenia trzeba dodać ręcznie.

---

## Sprzęt

### Zakupione urządzenia

| Urządzenie | Model | Źródło |
|------------|-------|--------|
| **Mikrokontroler** | ESP32-C6 DevKit (USB-C) | [Allegro](https://allegro.pl/oferta/modul-esp32-c6-mikrokontroler-wifi-6-zigbee-thread-ble-5-0-do-arduino-usb-c-18545765691) |
| **Moduł przekaźnika** | 8-Kanałowy, 5V, 10A, Active LOW | [Allegro](https://allegro.pl/oferta/modul-8-kanalowy-5v-10a-przekaznik-sterowany-stanem-niskim-0v-do-arduino-11435635416) |
| **Skrzynka zaworowa** | Jumbo 14" na 6 elektrozaworów | [Allegro](https://allegro.pl/oferta/skrzynka-studzienka-zaworowa-jumbo-14-6-elek-10549516399) |
| **Dysze rotacyjne** | Rain Bird R-VAN24 (45°–270°, 5,2–7,3m) | [Allegro](https://allegro.pl/oferta/dysza-rotacyjna-rain-bird-r-van-24-kat-regulowany-45-270-promien-5-2m-7-3m-12737230494) |
| **Kolektor** | Rain Bird HV-100 (6 sekcji + filtr + kompresor) | [Allegro](https://allegro.pl/oferta/kolektor-rain-bird-6-sekcji-hv-100-z-filtrem-i-kompresorem-rura-pe-25-17512626898) |

### ESP32-C6 DevKit — specyfikacja

| Parametr | Wartość |
|----------|---------|
| Procesor | RISC-V 32-bit, 160 MHz |
| RAM | 512 KB SRAM + 16 KB LP SRAM |
| Flash | 4–8 MB SPI |
| GPIO | 24 pinów (z 31 fizycznych) |
| WiFi | 6 (802.11ax, 2.4 GHz) |
| Bluetooth | 5.0 LE + Mesh |
| Zigbee | 3.0 (IEEE 802.15.4) |
| Thread | 1.3 (Matter over Thread) |
| Zasilanie | 3.3V (nie 5V tolerant!) |
| USB | 2× USB-C (UART + natywne USB 2.0) |

### Moduł przekaźnika 8CH — specyfikacja

| Parametr | Wartość |
|----------|---------|
| Kanały | 8 |
| Zasilanie | 5V DC |
| Prąd roboczy | ~400 mA (cały moduł) |
| Prąd wyzwalania | 3–5 mA na kanał |
| Wyzwalanie | Active LOW (0V = ON, 3.3V/5V = OFF) |
| Obciążenie | 10A @ 250VAC lub 10A @ 30VDC |
| Izolacja | Optoizolatory |
| Wymiary | 138 × 56 × 19.3 mm |

### Kompatybilność ESP32-C6 ↔ Przekaźnik

ESP32-C6 GPIO (3.3V) → Przekaźnik IN (Active LOW trigger) — **kompatybilne bez level shiftera**. Prąd wyzwalania 3–5 mA jest osiągalny z GPIO ESP32-C6 (max ~12 mA sink).

```
ESP32 GPIO (3.3V) ────→ IN1–IN8 (przekaźnika)
                          ↑
ESP32 5V (z USB) ────→ VCC (przekaźnika)
ESP32 GND ────────────→ GND (przekaźnika)
```

**Uwaga:** Przy 8 aktywnych przekaźnikach moduł pobiera ~400mA na 5V. Zalecany **osobny zasilacz 5V/2A+** dla przekaźnika.

### Bezpieczne piny GPIO (ESP32-C6)

| GPIO | Status | Uwagi |
|------|--------|-------|
| 0–6 | ADC, bezpieczne | GPIO0-3 użyte w kodzie do przekaźników |
| 4 | Strapping pin | Wymaga 10k pull-up (użyty do czujnika deszczu) |
| 8 | RGB LED (WS2812) | Nie obciążać przy starcie |
| 9 | BOOT button | Pull LOW = Download mode |
| 10, 11 | Bezpieczne | Użyte w kodzie do przekaźników |
| 14 | Bezpieczne | |
| 16, 17 | UART TX/RX | Przez CH343 USB-UART |
| 24–30 | Flash SPI | **NIE UŻYWAĆ** — wewnętrzny flash |

### Pinout GPIO

#### ESP32 (klasyczny, oryginalne okablowanie)

| GPIO | Funkcja |
|------|---------|
| 32 | Z0 — przekaźnik (Lewa) |
| 33 | Z1 — przekaźnik (Prawa) |
| 25 | Z2 — przekaźnik (Kroplująca lewa) |
| 26 | Z3 — przekaźnik (Tył) |
| 27 | Z4 — przekaźnik (Przód) |
| 14 | Z5 — przekaźnik (Rabaty) |
| 34 | Czujnik deszczu (INPUT_PULLUP, LOW = pada) |

#### ESP32-C6 (docelowy sprzęt)

| GPIO | Funkcja | Uwagi |
|------|---------|-------|
| 0 | Z0 — przekaźnik | ADC1_CH0 |
| 1 | Z1 — przekaźnik | ADC1_CH1 |
| 2 | Z2 — przekaźnik | ADC1_CH2 |
| 3 | Z3 — przekaźnik | ADC1_CH3 |
| 10 | Z4 — przekaźnik | Bezpieczny |
| 11 | Z5 — przekaźnik | Bezpieczny |
| 4 | Czujnik deszczu | Strapping pin — wymaga 10k pull-up |

Przekaźniki: active-LOW (PIN LOW = przekaźnik włączony, zawór otwarty).

### Schemat połączeń

```
┌─────────────────┐          ┌─────────────────────┐
│   ESP32-C6      │          │   8-CH Relay Module  │
│   DevKit        │          │                      │
│                 │          │                      │
│  GPIO0 ─────────┼──── IN1  │  Kanał 1 (Z0)       │
│  GPIO1 ─────────┼──── IN2  │  Kanał 2 (Z1)       │
│  GPIO2 ─────────┼──── IN3  │  Kanał 3 (Z2)       │
│  GPIO3 ─────────┼──── IN4  │  Kanał 4 (Z3)       │
│  GPIO10 ────────┼──── IN5  │  Kanał 5 (Z4)       │
│  GPIO11 ────────┼──── IN6  │  Kanał 6 (Z5)       │
│  GPIO14 ────────┼──── IN7  │  Kanał 7 ( nieużywane) │
│  GPIO16 ────────┼──── IN8  │  Kanał 8 ( nieużywane) │
│                 │          │                      │
│  5V  ───────────┼──── VCC  │  Zasilanie 5V        │
│  GND ───────────┼──── GND  │  Masa                │
│                 │          │                      │
│  GPIO4 ─────────┼──── CZUJNIK DESZCZU (10k pull-up) │
└─────────────────┘          └─────────────────────┘
```

### Zasilanie systemu

```
Zasilacz 5V/3A (osobny, zalecany)
  ├──→ Przekaźnik VCC (5V)
  └──→ ESP32-C6 USB-C (5V)
         └──→ 3.3V LDO → ESP32-C6

Obwód 230V AC (jeśli używasz pompy):
  Przekaźnik COM/NO → Pompa nawadniania
  Przekaźnik COM/NO → Zawór 1
  Przekaźnik COM/NO → Zawór 2
  ...
```

**Uwaga:** Przy pełnym obciążeniu (8 × 50mA = 400mA) nie zasilaj przekaźnika z pinu 5V ESP32. Użyj oddzielnego zasilacza 5V/2A+.

### Bezpieczeństwo sprzętu

1. **Napięcie sieciowe 230V** — przekaźniki przełączają wysokie napięcie. Zachowaj ostrożność!
2. **Oddziel zasilanie** — nie zasilaj przekaźnika z pinu 5V ESP32 przy pełnym obciążeniu
3. **Optoizolatory** — dla pełnej izolacji galwanicznej odłącz jumper VCC-JD-VCC i podaj osobne 5V do JD-VCC
4. **Obciążenie** — max 10A na kanał. Dla pompy nawadnianiowej (0.5–2A) to wystarczający zapas

---

## WiFi

- **Tryb AP**: `Nawodnienie-AP` (otwarty, 192.168.4.1) — do konfiguracji początkowej
- **Tryb STA**: łączy się do zapisanej sieci WiFi
- **mDNS**: `irrigation.local` (działa w obu trybach)
- ESP32 obsługuje tylko **2.4 GHz**

Po starcie: jeśli brak zapisanych danych WiFi lub błąd połączenia, ESP32 uruchamia hotspota AP.

---

## Partycje Flash (4MB)

Z `partitions.csv`:

| Partycja | Typ | Adres | Rozmiar |
|----------|-----|-------|---------|
| nvs | nvs | 0x9000 | 20 KB |
| otadata | ota | 0xe000 | 8 KB |
| app0 (ota_0) | app | 0x10000 | 1.25 MB |
| app1 (ota_1) | app | 0x150000 | 1.25 MB |
| spiffs | spiffs | 0x290000 | 1.375 MB |
| coredump | coredump | 0x3F0000 | 64 KB |

OTA przełącza między `app0` a `app1`. Obraz web UI (React) jest w partycji `spiffs`.

---

## Komendy Makefile

| Komenda | Co robi |
|---------|---------|
| `make help` | Lista wszystkich komend |
| `make build` | Buduje firmware + web UI + SPIFFS image |
| `make flash` | Buduje i wgraj przez USB |
| `make monitor` | Podgląd logów szeregowych (Ctrl-] aby wyjść) |
| `make run` | Build + flash + monitor (codzienne flow) |
| `make ota` | Aktualizacja firmware przez sieć (na HOST=irrigation.local) |
| `make setup-wifi SSID=... PASS=...` | Zmiana WiFi + restart |
| `make info` | Status urządzenia (/api/info) |
| `make verify` | Szybka weryfikacja WiFi (tryb/IP/SSID) |
| `make web-install` | Instalacja zależności web UI (jednorazowo) |
| `make web` | Dev server web UI (Vite, proxy /api do urządzenia) |
| `make erase` | Pełne czyszczenie flasha |
| `make clean` | Czyszczenie artefaktów buildu |
| `make port` | Pokaż wykryty port USB |
| `make target-c6` | Ustaw target na ESP32-C6 |
| `make target-esp32` | Ustaw target na klasyczny ESP32 |

Nadpisywalne zmienne:
- `PORT` — port USB (auto-wykrywany na macOS)
- `HOST` — adres urządzenia w sieci (domyślnie `irrigation.local`)
- `SSID`, `PASS` — dane WiFi dla `setup-wifi`

---

## Rozwiązywanie problemów

| Problem | Rozwiązanie |
|---------|-------------|
| `irrigation.local` nie odpowiada | Sprawdź czy ESP jest w sieci WiFi. Użyj `make verify` |
| Nie mogę się połączyć z WiFi | ESP32 obsługuje tylko 2.4 GHz. Sprawdź hasło |
| `make flash` nie widzi portu | Podłącz USB, sprawdź `make port` |
| OTA nie działa | Sprawdź czy ESP jest w sieci. Użyj `make ota HOST=<ip>` |
| Panel webowy nie działa | Sprawdź czy SPIFFS został wgrany. Użyj `make run` |
| `make build` błędem npm | Zainstaluj Node.js 18+. Użyj `make web-install` |
| Urządzenie się nie wznawia po OTA | ESP-IDF ma auto-rollback. Sprawdź logi `make monitor` |
| Deszczownik nie reaguje | Sprawdź połączenie z GPIO4 (C6) lub GPIO34 (ESP32). LOW = pada |
| Przekaźnik nie włącza się | Sprawdź czy 5V jest podane na VCC. Sprawdź czy GPIO jest LOW (Active LOW) |
| Przekaźnik się nie załącza z ESP32 | ESP32-C6 ma 3.3V na GPIO — sprawdź czy moduł ma optoizolatory (Active LOW) |
| Za dużo prądu na 5V z ESP32 | Użyj oddzielnego zasilacza 5V/2A+ dla przekaźnika |

---

## Przydatne pliki

| Plik | Opis |
|------|------|
| `hardware-docs/ESP32-C6-Relay-8CH-Documentation.md` | Dokumentacja sprzętu (ESP32-C6 + przekaźnik) |
| `hardware-docs/Sprzet-Nawadniania-Documentation.md` | Dokumentacja sprzętu (skrzynka Jumbo, dysze R-VAN24, kolektor HV-100) |
| `docs/FLASHING.md` | Szczegółowy przewodnik wgrywania |
| `docs/schematics.md` | Schemat połączeń |
| `docs/PLAN.md` | Plan rozwoju |
| `docs/parts_list.md` | Lista części |
| `Makefile` | Skróty make |
| `CMakeLists.txt` | Build system + web UI pipeline |

---

## Bezpieczeństwo

- Panel webowy nie wymaga autoryzacji (lokalna sieć)
- MQTT może wymagać logowania (konfigurowalne)
- OTA wymaga fizycznego dostępu do sieci
- Zmiana WiFi restartuje urządzenie
- Watchdog i max czas strefy chronią przed zawieszeniem

### Bezpieczeństwo sprzętu

1. **Napięcie sieciowe 230V** — przekaźniki przełączają wysokie napięcie. Zachowaj ostrożność!
2. **Oddziel zasilanie** — nie zasilaj przekaźnika z pinu 5V ESP32 przy pełnym obciążeniu (8 × 50mA = 400mA). Użyj oddzielnego zasilacza 5V/2A+.
3. **Optoizolatory** — jeśli chcesz pełnej izolacji galwanicznej, odłącz jumper VCC-JD-VCC i podaj osobne 5V do JD-VCC.
4. **Obciążenie** — max 10A na kanał. Dla pompy nawadnianiowej (0.5–2A) to wystarczający zapas.
5. **Strapping pins** — GPIO0, GPIO4, GPIO5, GPIO8, GPIO9, GPIO15 mają specjalne funkcje przy starcie. Unikaj ich jako output jeśli nie wiesz co robisz.
6. **Flash SPI** — GPIO24-30 są podłączone do wewnętrznego flasha. **NIE UŻYWAJ** ich jako GPIO.
