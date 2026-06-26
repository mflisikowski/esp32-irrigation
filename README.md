# 💧 System Nawodnienia Ogrodu — ESP32

Automatyczne nawadnianie ogrodu sterowane ESP32 z panelem webowym i integracją Home Assistant (MQTT).

## Strefy

| # | Nazwa | Pow. | Typ | Zraszacze |
|---|-------|------|-----|-----------|
| Z1 | S2a — trawnik lewy | ~55 m² | Rotacyjne | 2× Rain Bird 5000 PC |
| Z2 | S2b — trawnik prawy | ~55 m² | Rotacyjne | 2× Rain Bird 5000 PC |
| Z3 | S1+S3 — trawniki | ~43 m² | Statyczne | 4× Rain Bird 1800 + MP Rotator |
| Z4 | S4 — bok | ~25 m² | Wąskostrumieniowe | 3× Rain Bird 1800 |
| Z5 | S5 — front | ~27 m² | Statyczne | 3× Rain Bird 1800 |
| Z6 | Rabaty | ~45 m.b. | Kroplujące | Linia PE16 + reduktor |

## Wymagany sprzęt

### Sterowanie
- ESP32 C6
- Moduł przekaźników 8ch 5V (active LOW)
- Zasilacz 24V AC 2A (do zaworów)
- Zasilacz 5V 2A (do ESP32) lub HLK-PM01
- Czujnik deszczu FC-37 lub RSD-BEx
- Obudowa IP65 300×250×150mm
- Przewód sterujący YDYp 7×1mm²

### Hydraulika
- Zawory elektromagnetyczne Rain Bird DV-100 1" × 6
- Filtr siatkowy 1" 120µ
- Reduktor ciśnienia 1.5 bar (do kroplowania)
- Rura PE32 (magistrala) — 25m
- Rura PE25 (odgałęzienia) — 50m
- Linia kroplująca PE16 — 50m
- Skrzynka zaworowa (valve box) + fittingi

## Instalacja firmware

1. Zainstaluj [PlatformIO](https://platformio.org/) (lub [Arduino IDE](https://www.arduino.cc/en/software/))
2. Otwórz folder `firmware/`
3. Skonfiguruj MQTT i strefy w `src/main.cpp` (sekcja `DEFAULT_CONFIG`)
4. Podłącz ESP32 przez USB
5. Kompiluj i wgraj: `pio run -t upload` lub upload przez Arduino IDE
6. Monitor: `pio device monitor` (115200 baud)

### Pierwsze uruchomienie
1. ESP32 utworzy hotspot WiFi `Nawodnienie-AP`
2. Połącz się z nim i skonfiguruj domową sieć WiFi
3. ESP32 zrestartuje się i poda IP w monitorze szeregowym
4. Otwórz IP w przeglądarce

## Funkcje firmware

- **Panel webowy** — sterowanie manualne, timer, status
- **Harmonogram** — 6 stref, dni tygodnia, godzina startu, czas podlewania
- **Czujnik deszczu** — automatyczne wyłączenie przy deszczu
- **MQTT** — integracja z Home Assistant (auto-discovery)
- **NTP** — synchronizacja czasu
- **SSE** — live status na stronie (bez odświeżania)
- **Ochrona** — watchdog, max czas strefy, rain override

## API HTTP

| Ścieżka | Metoda | Opis |
|---------|--------|------|
| `/` | GET | Panel webowy |
| `/api/zones` | GET | Status stref (JSON) |
| `/api/schedule` | GET | Harmonogram (JSON) |
| `/api/rain` | GET | Status deszczu |
| `/api/zone/{id}/command?action=on` | GET | Włącz strefę |
| `/api/zone/{id}/command?action=off` | GET | Wyłącz strefę |
| `/api/zone/{id}/command?action=run&seconds=300` | GET | Uruchom na X sekund |
| `/api/all/off` | GET | Wyłącz wszystko |
| `/api/save` | POST | Zapisz harmonogram (body: JSON) |
| `/api/info` | GET | System info |

## MQTT Home Assistant

Auto-discovery topics:

| Topic | Typ | Opis |
|-------|-----|------|
| `irrigation/zone/{id}/status` | switch | Stan strefy |
| `irrigation/zone/{id}/command` | command | Sterowanie |
| `irrigation/rain` | binary_sensor | Czujnik deszczu |

## Etapy montażu

### Etap 1 — Główny trawnik (~700 zł)
- [ ] Magistrala PE32 od przyłącza do rozdzielacza
- [ ] 4× zawór DV-100
- [ ] Skrzynka zaworowa + filtr
- [ ] PE25 do S2a i S2b
- [ ] 4× Rain Bird 5000 PC
- [ ] Sterowanie ręczne (zawory otwierane ręcznie)

### Etap 2 — Reszta trawników tył (~350 zł)
- [ ] Przedłużenie PE25 do S1 + S3
- [ ] 4× Rain Bird 1800 + MP Rotator
- [ ] 2× zawór DV-100

### Etap 3 — Bok + front (~450 zł)
- [ ] PE25 wzdłuż ściany domu → S4 + S5
- [ ] 3× RB 1800 wąskie (S4) + 3× RB 1800 (S5)
- [ ] Obejście/przejście przez piwnicę

### Etap 4 — Rabaty kroplujące (~300 zł)
- [ ] Reduktor ciśnienia 1.5 bar
- [ ] Linia kroplująca PE16 wzdłuż rabat
- [ ] Kroplowniki co 30cm (kompensujące ciśnienie)

### Etap 5 — Automatyka ESP32 (~350 zł)
- [ ] ESP32 + przekaźniki + zasilacz + obudowa
- [ ] Przewody sterujące YDYp 7×1mm² do zaworów
- [ ] Czujnik deszczu
- [ ] Wgranie firmware i konfiguracja
