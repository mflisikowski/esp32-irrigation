# Plan rozwoju systemu nawodnienia ESP32

## Cel

Przekształcenie monolitycznego firmware ESP32 (inline HTML + API) na nowoczesną architekturę:
- **ESP32** = backend (API + sterowanie przekaźnikami + czujniki)
- **React** = frontend (panel webowy serwowany z SPIFFS)
- **OTA** = aktualizacja firmware przez WiFi

## Architektura docelowa

```
[React App] → build → [SPIFFS] → ESP32 serwuje pliki
     ↓
Przeglądarka ←HTTP/WS→ ESP32 (API + przekaźniki + czujniki)
```

## Etapy realizacji

### Etap 1: Struktura projektu [~1h]

**Cel:** Przygotować folder `web/` i skrypty buildowe.

**Zadania:**
1. Utworzyć strukturę folderów:
   ```
   firmware/
   ├── src/
   │   └── main.cpp
   ├── web/
   │   ├── src/
   │   ├── public/
   │   ├── package.json
   │   └── vite.config.ts
   ├── data/              ← auto-generowane
   ├── extra_script.py
   ├── partitions.csv
   └── platformio.ini
   ```

2. Utworzyć `extra_script.py`:
   ```python
   Import("env")

   def before_build_spiffs(source, target, env):
       env.Execute("cd web && npm run build")
       env.Execute("rm -rf data")
       env.Execute("cp -r web/build data")

   env.AddPreAction("$BUILD_DIR/spiffs.bin", before_build_spiffs)
   ```

3. Zaktualizować `platformio.ini`:
   ```ini
   board_build.partitions = no_ota.csv
   extra_scripts = extra_script.py
   lib_deps = ... (bez zmian)
   ```

4. Dodać SPIFFS do main.cpp:
   ```cpp
   #include "SPIFFS.h"
   // w setup():
   if (!SPIFFS.begin(true)) {
       Serial.println("SPIFFS mount failed");
       return;
   }
   server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
   ```

5. Dodać CORS:
   ```cpp
   DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
   DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
   DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
   ```

**Plik wyjściowy:** Sprawdzony build, ESP32 serwuje pustą stronę z SPIFFS.

---

### Etap 2: Inicjalizacja React App [~1h]

**Cel:** Stworzyć projekt React (Vite) w folderze `web/`.

**Zadania:**
1. Zainicjalizować projekt:
   ```bash
   cd firmware/web
   npm create vite@latest . -- --template react-ts
   npm install
   ```

2. Zainstalować zależności:
   ```bash
   npm install axios
   npm install -D @types/node
   ```

3. Skonfigurować `vite.config.ts`:
   ```typescript
   export default defineConfig({
     base: '/',  // ważne dla ESP32
     build: {
       outDir: 'build',
       assetsDir: 'static',
     },
     server: {
       proxy: {
         '/api': 'http://192.168.88.25',  // ESP32 IP
       }
     }
   })
   ```

4. Zmodyfikować `package.json` script build:
   ```json
   "scripts": {
     "dev": "vite",
     "build": "vite build && mv build/assets/* build/static/ 2>/dev/null; mkdir -p build/static && mv build/assets/* build/static/ 2>/dev/null || true",
     "preview": "vite preview"
   }
   ```

5. Utworzyć podstawową strukturę komponentów:
   ```
   web/src/
   ├── App.tsx
   ├── components/
   │   ├── Header.tsx
   │   ├── ZoneCard.tsx
   │   ├── ZoneGrid.tsx
   │   ├── ScheduleTable.tsx
   │   └── StatusBar.tsx
   ├── api/
   │   └── esp32.ts
   ├── types/
   │   └── index.ts
   └── main.tsx
   ```

**Plik wyjściowy:** `npm run build` generuje `build/` z poprawnymi ścieżkami.

---

### Etap 3: Type definitions i API client [~30min]

**Cel:** Zdefiniować typy i klienta API.

**Zadania:**

1. Utworzyć `types/index.ts`:
   ```typescript
   export interface Zone {
     id: number;
     name: string;
     active: boolean;
     remaining: number;
     enabled: boolean;
   }

   export interface Schedule {
     id: number;
     name: string;
     enabled: boolean;
     days: number;
     startHour: number;
     startMinute: number;
     duration: number;
   }

   export interface SystemInfo {
     version: string;
     time: string;
     synced: boolean;
     uptime: number;
     heap: number;
     mqtt: boolean;
     rain: boolean;
   }
   ```

2. Utworzyć `api/esp32.ts`:
   ```typescript
   import axios from 'axios';
   import { Zone, Schedule, SystemInfo } from '../types';

   const api = axios.create({
     baseURL: '/api',
   });

   export const getZones = () => api.get<Zone[]>('/zones');
   export const getSchedule = () => api.get<Schedule[]>('/schedule');
   export const getRain = () => api.get<{ rain: boolean }>('/rain');
   export const getInfo = () => api.get<SystemInfo>('/info');

   export const zoneCommand = (id: number, action: string, seconds?: number) => {
     const params = seconds ? `${action}&seconds=${seconds}` : action;
     return api.get(`/zone/${id}/command?action=${params}`);
   };

   export const allOff = () => api.get('/all/off');
   export const saveSchedule = (schedule: Schedule[]) => api.post('/save', schedule);
   ```

**Plik wyjściowy:** Typowany klient API gotowy do użycia w komponentach.

---

### Etap 4: Komponenty React [~2h]

**Cel:** Stworzyć interfejs webowy.

**Komponenty do zaimplementowania:**

1. **Header.tsx** — status systemu (czas, deszcz, MQTT)
2. **ZoneCard.tsx** — karta strefy (nazwa, status, przyciski)
3. **ZoneGrid.tsx** — siatka kart stref
4. **ScheduleTable.tsx** — tabela harmonogramu
5. **StatusBar.tsx** — pasek statusu na górze

**Wzorce:**
- Używać `useEffect` do pobierania danych z API
- Polling co 2s dla live status (lub WebSocket w późniejszym etapie)
- `useState` dla lokalnego stanu formularzy

**Plik wyjściowy:** Działający panel webowy z podstawową funkcjonalnością.

---

### Etap 5: Usunięcie inline HTML [~30min]

**Cel:** Usunąć stary HTML z main.cpp.

**Zadania:**
1. Usunąć `const char PAGE_HTML[] PROGMEM = ...` z main.cpp
2. Usunąć endpoint `GET /` (teraz serwowany z SPIFFS)
3. Usunąć niepotrzebne zmienne i funkcje związane z HTML
4. Sprawdzić czy wszystkie API endpoints działają

**Plik wyjściowy:** main.cpp bez inline HTML, tylko API.

---

### Etap 6: OTA (opcjonalnie) [~1h]

**Cel:** Dodać aktualizację firmware przez WiFi.

**Zadania:**

1. Utworzyć `partitions.csv`:
   ```csv
   nvs,      data, nvs,     0x9000,   0x5000
   otadata,  data, ota,     0xe000,   0x2000
   app0,     app,  ota_0,   0x10000,  0x200000
   app1,     app,  ota_1,   0x210000, 0x200000
   spiffs,   data, spiffs,  0x410000, 0x1E0000
   coredump, data, coredump,0x5F0000, 0x10000
   ```

2. Zaktualizować `platformio.ini`:
   ```ini
   board_build.partitions = partitions.csv
   ```

3. Dodać endpoint OTA w main.cpp:
   ```cpp
   #include <Update.h>

   server.on("/api/ota", HTTP_POST,
     [](AsyncWebServerRequest *request) {
       request->send(200);
     },
     [](AsyncWebServerRequest *request, String filename, size_t index,
        uint8_t *data, size_t len, bool final) {
       if (!index) {
         Update.begin(UPDATE_SIZE_UNKNOWN);
       }
       Update.write(data, len);
       if (final) {
         Update.end(true);
         ESP.restart();
       }
     }
   );
   ```

4. Dodać frontend upload (opcjonalnie):
   - Input type="file" dla .bin
   - Progress bar
   - Przycisk "Update"

**Plik wyjściowy:** OTA działa, aktualizacja przez przeglądarkę.

---

## Priorytet realizacji

| Krok | Etap | Nakład | Zależności |
|------|------|--------|------------|
| 1 | Struktura projektu | 1h | Brak |
| 2 | React App init | 1h | Etap 1 |
| 3 | Type definitions | 30min | Etap 2 |
| 4 | Komponenty React | 2h | Etap 3 |
| 5 | Usunięcie inline HTML | 30min | Etap 4 |
| 6 | OTA | 1h | Etap 5 (opcjonalnie) |

**Łączny nakład:** ~5-6h (bez OTA) lub ~6-7h (z OTA)

---

## Weryfikacja

Po każdym etapie sprawdzić:
- [ ] `pio run` — build przechodzi
- [ ] `pio run -t upload` — firmware się wgrywa
- [ ] ESP32 łączy się z WiFi
- [ ] API endpoints działają (`curl`)
- [ ] Panel webowy się wyświetla (po etapach 4-5)

---

## Ryzyka

| Ryzyko | Mitigacja |
|--------|-----------|
| Za mało miejsca w SPIFFS | Użyć `no_ota.csv` lub 8MB flash |
| CORS blokuje React dev server | Dodać CORS headers w ESP32 |
| WebSocket nie działa | Zostać na polling (co 2s) |
| Build Reacta za wolny | Cache w CI/CD |

---

## Pliki referencyjne

- [React on ESP32 - artykuł](https://blockdev.io/react-on-the-esp32/)
- [ESP32 OTA workflow](references/ota-workflow.md)
- [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
- [Vite + React](https://vitejs.dev/guide/)
