# Wgrywanie firmware, OTA i diagnostyka — pełne flow

Praktyczny przewodnik „co kliknąć / co wpisać", żeby zbudować, wgrać i
zweryfikować firmware bez pamiętania komend `idf.py`. Wszystkie skróty siedzą
w **`Makefile`** w katalogu głównym repo — uruchamiasz je przez `make <cel>`.

> `make` bez argumentu (albo `make help`) wypisuje listę komend wraz z aktualnie
> wykrytym portem USB i adresem sieciowym urządzenia.

## Wymagania jednorazowe

1. **ESP-IDF v5.3+** zainstalowane w `~/esp/esp-idf` (Makefile sam je źródłuje
   przy każdej komendzie — nie musisz odpalać `export.sh` ręcznie).
2. **Zależności web UI** (do builda frontendu): `make web-install`.
3. **Wybrany target** — przy pierwszym buildzie ustaw sprzęt:
   - `make target-esp32` — klasyczny ESP32 (płytka deweloperska),
   - `make target-c6` — ESP32-C6 (sprzęt docelowy).

   Zmiana targetu przebudowuje `sdkconfig` i czyści build (robisz to rzadko).

## Ściąga komend (`make ...`)

| Komenda | Co robi |
|---------|---------|
| `make help` | Lista komend + wykryty port + host sieciowy |
| `make build` | Buduje firmware **i** web UI (bez wgrywania) |
| `make flash` | Buduje i wgrywa firmware przez USB |
| `make monitor` | Logi szeregowe @115200 (Ctrl-] aby wyjść) |
| `make run` | **Codzienne flow:** wgraj + od razu pokaż logi |
| `make erase` | Pełne czyszczenie flasha (po zmianie partycji) |
| `make clean` | Czyści artefakty buildu firmware |
| `make ota` | Buduje i wgrywa firmware **przez sieć** (OTA) |
| `make setup-wifi SSID=… PASS=…` | Zmienia WiFi urządzenia |
| `make info` | Pełny status urządzenia (`/api/info`) |
| `make verify` | Szybka weryfikacja WiFi (tryb/IP/SSID) |
| `make port` | Pokazuje wykryty port USB |
| `make web` | Dev server web UI (Vite) |
| `make web-install` | Instaluje zależności web UI |
| `make target-esp32` / `make target-c6` | Wybór sprzętu |

### Nadpisywalne zmienne

Każdą komendę można doprecyzować zmienną w tej samej linii:

```bash
make flash PORT=/dev/cu.usbserial-20     # gdy auto-detekcja wybierze zły port
make ota   HOST=192.168.1.42             # gdy mDNS nie działa, podaj IP
make setup-wifi SSID=Internet PASS=tajne
```

- **`PORT`** — port USB-UART. Domyślnie auto-wykrywany (pierwszy `usbserial`/
  `wchusbserial`/`SLAB_USBtoUART`/`usbmodem`). Sprawdź `make port`.
- **`HOST`** — adres urządzenia w sieci. Domyślnie `irrigation.local` (mDNS).

## Typowe scenariusze

### Pierwsze wgranie (przez USB)

```bash
make target-c6      # lub target-esp32 — raz, pod swój sprzęt
make web-install    # raz
make run            # build + flash + monitor
```

W logach zobaczysz `WiFi OK: <IP>` (połączono) albo `WiFi FAIL — uruchamiam
AP...` (start hotspotu `Nawodnienie-AP`).

### Codzienna iteracja kodu

```bash
make run            # przebuduje, wgra i pokaże logi
```

### Aktualizacja bez kabla (OTA)

Gdy urządzenie jest już w sieci, nie musisz podłączać USB:

```bash
make ota                      # przez mDNS (irrigation.local)
make ota HOST=192.168.1.42    # albo po IP
```

`make ota` najpierw buduje firmware, potem wysyła `build/irrigation.bin` na
`POST /api/ota`. Po sukcesie urządzenie samo się restartuje i waliduje obraz
(brak automatycznego rollbacku).

## Zmiana WiFi i weryfikacja połączenia

Endpoint `POST /api/wifi` zapisuje dane logowania i **restartuje** urządzenie —
sama zwrotka `{"ok":true}` potwierdza tylko zapis, nie udane połączenie. Po
restarcie urządzenie dostaje **nowy adres IP z DHCP** (często w innej podsieci),
więc stary adres przestaje odpowiadać.

Dlatego do weryfikacji służy **mDNS + rozszerzone `/api/info`**:

```bash
make setup-wifi SSID=Internet PASS=tajnehaslo
# ... urządzenie restartuje (kilka sekund) ...
make verify
```

`make verify` pyta `http://irrigation.local/api/info` i pokazuje:

```json
{ "wifiMode": "STA", "ip": "192.168.1.42", "ssid": "Internet", "version": "...", "synced": true }
```

Interpretacja:

- **`wifiMode: "STA"`** + Twoje `ssid` + IP z Twojej podsieci → **połączono OK**.
- **`wifiMode: "AP"`** → logowanie **nieudane**, urządzenie wystawiło hotspot
  `Nawodnienie-AP`. Połącz się z nim i ponów `make setup-wifi` (sprawdź literówki).
- **Brak odpowiedzi** → urządzenie offline albo Twój komputer nie rozwiązuje
  `.local`; spróbuj po IP z tablicy DHCP routera: `make verify HOST=<ip>`.

> **mDNS** (`irrigation.local`) jest ogłaszany w trybie STA i AP, więc trafiasz
> do urządzenia bez znajomości IP. macOS rozwiązuje `.local` natywnie (Bonjour).

### Konfiguracja, gdy urządzenie jest w trybie AP

Jeśli ESP nie ma jeszcze poprawnych danych WiFi (albo logowanie zawiodło),
spada do **hotspotu `Nawodnienie-AP`** pod stałym adresem **`192.168.4.1`** —
widać to w logach: `WiFi FAIL — uruchamiam AP...` / `AP IP: 192.168.4.1`.

W tym stanie `irrigation.local` **nie rozwiąże się** z Twojego komputera, bo
komputer siedzi na innej sieci niż hotspot ESP (mDNS przez to nie przeskakuje,
stąd `curl: (6) Could not resolve host: irrigation.local`). Trzeba dołączyć do
sieci urządzenia i celować po IP:

1. Na komputerze połącz się z WiFi **`Nawodnienie-AP`** (otwarta, bez hasła) —
   dostaniesz adres `192.168.4.x`, urządzenie jest pod `192.168.4.1`.
2. Wyślij dane WiFi po IP (nie po mDNS):
   ```bash
   make setup-wifi SSID=Internet PASS=tajnehaslo HOST=192.168.4.1
   ```
3. ESP zapisuje i restartuje. **Najszybsza weryfikacja: otwarty monitor
   szeregowy przez USB** (`make monitor`) — po restarcie zobaczysz wprost
   `WiFi OK: <IP>` albo ponowny `WiFi FAIL`.
4. Po `WiFi OK` przełącz komputer z powrotem na domową sieć i `make verify`
   (lub `make verify HOST=<ip z monitora>`, jeśli `.local` marudzi).

> Powtarzający się w logach `wifi: auth -> init` to **odrzucone uwierzytelnienie**
> (ESP widzi sieć, ale nie wchodzi). Najczęściej: złe hasło, sieć tylko na **5 GHz**
> (klasyczny ESP32 / C6 obsługują **wyłącznie 2.4 GHz**) albo filtr MAC na routerze.

## Rozwiązywanie problemów

| Objaw | Co zrobić |
|-------|-----------|
| `make flash` nie widzi portu | `make port`; podłącz USB lub `make flash PORT=/dev/cu.…` |
| Kilka urządzeń USB naraz | Wskaż jawnie `PORT=…` (auto-detekcja bierze pierwszy) |
| `irrigation.local` nie odpowiada | Urządzenie w trybie AP (inna sieć) lub `.local` nie działa — patrz „Konfiguracja w trybie AP", albo `make verify HOST=<ip>` |
| `curl: (6) Could not resolve host` | Jesteś na innej sieci niż ESP — dołącz do `Nawodnienie-AP` i celuj `HOST=192.168.4.1` |
| OTA zwraca błąd / timeout | Sprawdź `make info`; przy zmianie partycji wymagany `make erase` + flash USB |
| Po `setup-wifi` cisza / `WiFi FAIL` | Urządzenie w trybie AP — patrz „Konfiguracja, gdy urządzenie jest w trybie AP" |
| Zmiana targetu nie działa | `make target-c6` / `make target-esp32` przebudowuje sdkconfig + czyści build |

## Pod spodem (gdyby `make` był niedostępny)

Każdy cel to cienka nakładka na `idf.py`/`curl`. Odpowiedniki ręczne:

```bash
. $HOME/esp/esp-idf/export.sh
idf.py -p <PORT> flash monitor                      # = make run
curl -X POST -H "Content-Type: application/octet-stream" \
     --data-binary @build/irrigation.bin http://irrigation.local/api/ota   # = make ota
curl -fs http://irrigation.local/api/info | jq .    # = make info
```
