# Third-party IDF components

Each subdirectory is a standalone ESP-IDF component, registered via
`EXTRA_COMPONENT_DIRS` in the project root `CMakeLists.txt`.

Do **not** `include()` these from `main/` — each must call `idf_component_register` itself.

- `littlefs` — joltwallet/esp_littlefs v1.21.1 (local). `example/` and
  `src/littlefs/scripts/` removed. Require as `littlefs`.
