# Makefile — wygodne skróty do całego flow (firmware ESP-IDF + web UI).
#
# Cel: nie wklepywać `idf.py ...` z pamięci. Uruchamiasz `make <cel>`.
# `make` bez argumentu pokazuje listę dostępnych komend (make help).
#
# Nadpisywalne zmienne (np. `make flash PORT=/dev/cu.usbserial-20`):
#   PORT  — port szeregowy USB (domyślnie auto-wykrywany)
#   HOST  — adres urządzenia w sieci dla OTA/diagnostyki (domyślnie mDNS)
#   SSID, PASS — dane logowania WiFi dla celu `setup-wifi`

# ── Konfiguracja ────────────────────────────────────────────────────────────
# Wczytaj środowisko ESP-IDF raz, w tym samym shellu co idf.py (każda linia
# recepty to osobny shell, więc źródłujemy inline przed każdą komendą idf.py).
IDF := . $$HOME/esp/esp-idf/export.sh >/dev/null 2>&1 &&

# Auto-wykrycie portu USB-UART na macOS (pomija Bluetooth/debug-console).
# Bierze pierwszy pasujący; nadpisz `PORT=...` jeśli masz kilka.
PORT ?= $(shell ls /dev/cu.usbserial-* /dev/cu.wchusbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.usbmodem* 2>/dev/null | head -n1)
PORT_ARG := $(if $(PORT),-p $(PORT),)

# Adres urządzenia w sieci (mDNS z wifi_mgr.c). Nadpisz IP, jeśli mDNS nie działa.
HOST ?= irrigation.local

.DEFAULT_GOAL := help
.PHONY: help build web web-install flash monitor run erase clean \
        ota setup-wifi info verify port target-esp32 target-c6

# ── Pomoc ───────────────────────────────────────────────────────────────────
help: ## Pokaż tę listę komend
	@echo "Irrigation — dostępne komendy (make <cel>):"
	@echo ""
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) \
		| awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-13s\033[0m %s\n",$$1,$$2}'
	@echo ""
	@echo "Wykryty port: $(if $(PORT),$(PORT),BRAK — podłącz USB lub podaj PORT=...)"
	@echo "Host sieciowy: $(HOST)"

# ── Budowanie / wgrywanie (USB) ──────────────────────────────────────────────
build: ## Zbuduj firmware + web UI (bez wgrywania)
	@$(IDF) idf.py build

flash: ## Zbuduj i wgraj firmware przez USB
	@$(IDF) idf.py $(PORT_ARG) flash

monitor: ## Podgląd logów szeregowych (Ctrl-] aby wyjść)
	@$(IDF) idf.py $(PORT_ARG) monitor

run: ## Wgraj firmware i od razu pokaż logi (codzienne flow)
	@$(IDF) idf.py $(PORT_ARG) flash monitor

erase: ## Pełne czyszczenie flasha (po zmianie partycji)
	@$(IDF) idf.py $(PORT_ARG) erase-flash

clean: ## Wyczyść artefakty buildu firmware
	@$(IDF) idf.py fullclean

# ── Aktualizacja przez sieć (OTA) ────────────────────────────────────────────
ota: build ## Wgraj firmware przez sieć (OTA) na HOST=$(HOST)
	@echo "OTA → http://$(HOST)/api/ota"
	@curl -f --progress-bar -X POST \
		-H "Content-Type: application/octet-stream" \
		--data-binary @build/irrigation.bin \
		http://$(HOST)/api/ota && echo "\nOTA OK — urządzenie się zrestartuje."

# ── WiFi / diagnostyka (sieć) ────────────────────────────────────────────────
setup-wifi: ## Zmień WiFi: make setup-wifi SSID=Internet PASS=haslo
	@test -n "$(SSID)" || { echo "Brak SSID. Użyj: make setup-wifi SSID=... PASS=..."; exit 1; }
	@curl -f -X POST http://$(HOST)/api/wifi \
		-H "Content-Type: application/json" \
		-d '{"ssid":"$(SSID)","pass":"$(PASS)"}' \
		&& echo "\nZapisano + restart. Sprawdź: make verify"

info: ## Pełny status urządzenia (/api/info) z HOST=$(HOST)
	@curl -fs http://$(HOST)/api/info | jq .

verify: ## Szybka weryfikacja połączenia WiFi (tryb/IP/SSID)
	@echo "Pytam http://$(HOST)/api/info ..."
	@curl -fs http://$(HOST)/api/info \
		| jq '{wifiMode, ip, ssid, version, synced}' \
		|| echo "Brak odpowiedzi — urządzenie offline lub w trybie AP (Nawodnienie-AP)."

port: ## Pokaż wykryty port USB
	@echo "$(if $(PORT),$(PORT),BRAK — podłącz USB lub podaj PORT=...)"

# ── Web UI ───────────────────────────────────────────────────────────────────
web-install: ## Zainstaluj zależności web UI (jednorazowo)
	@cd web && npm install

web: ## Dev server web UI (Vite, proxy /api do urządzenia)
	@cd web && npm run dev

# ── Wybór targetu (przebudowuje sdkconfig, czyści build) ─────────────────────
target-esp32: ## Ustaw target na klasyczny ESP32 (płytka dev)
	@$(IDF) idf.py set-target esp32

target-c6: ## Ustaw target na ESP32-C6 (docelowy sprzęt)
	@$(IDF) idf.py set-target esp32c6
