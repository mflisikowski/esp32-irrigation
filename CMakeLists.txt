# ESP-IDF root project — ESP32-C6 irrigation controller (native rewrite of src/main.cpp).
# Built/verified against ESP-IDF v5.3.x.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(irrigation)

# ─── Web UI build pipeline (replaces extra_script.py) ───────────────────────
# Builds the React/Vite frontend and copies web/build → data/, which is then
# packed into the SPIFFS image below. Kept NON-FATAL during isolated builds:
# if npm is unavailable or the build fails, the already-present data/ is reused
# so the SPIFFS image step still succeeds. Integration (WP5) may tighten this.
add_custom_target(web_build
  COMMAND bash -c "npm --prefix '${CMAKE_SOURCE_DIR}/web' run build \
    || echo 'WP1: web build skipped/failed — reusing existing data/'"
  COMMAND bash -c "if [ -d '${CMAKE_SOURCE_DIR}/web/build' ]; then \
    rm -rf '${CMAKE_SOURCE_DIR}/data' \
    && cp -R '${CMAKE_SOURCE_DIR}/web/build' '${CMAKE_SOURCE_DIR}/data'; \
    fi"
  COMMENT "Building web UI and copying web/build -> data/"
  VERBATIM)

# Pack data/ into the 'spiffs' partition image and flash it with `idf.py flash`.
spiffs_create_partition_image(spiffs ${CMAKE_SOURCE_DIR}/data FLASH_IN_PROJECT DEPENDS web_build)
