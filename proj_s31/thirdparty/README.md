# Third-party IDF components

Each subdirectory is a standalone ESP-IDF component, registered via
`EXTRA_COMPONENT_DIRS` in the project root `CMakeLists.txt`.

Do **not** `include()` these from `main/` — each must call `idf_component_register` itself.

- `littlefs` — joltwallet/esp_littlefs v1.21.1 (local). `example/` and
  `src/littlefs/scripts/` removed. Require as `littlefs`.
- `ethernet_init` — espressif/ethernet_init 1.4.1 (local, no registry).
  Linked only when `CONFIG_APP_INIT_ETHERNET` is enabled. Other PHY/SPI
  chips from the original component YAML are not vendored.
- `yt8531` — espressif/yt8531 0.2.0 (local, no registry). Required by
  `ethernet_init` when `CONFIG_ETHERNET_PHY_YT8531` is set.

To upgrade Ethernet, copy a newer tree into these directories; do not use
`idf.py update-dependencies` for them. After the first copy, Ethernet
builds do not need `components.espressif.com`.
