# ESP-IDF Agent Notes

Repo-specific context for OpenCode sessions working on Espressif's IoT Development Framework (ESP-IDF).

## Environment

- This is the ESP-IDF framework itself, not a standalone app. The root `CMakeLists.txt` refuses to build directly; always build from an `examples/` or `tools/test_apps/` project.
- Every shell needs the ESP-IDF environment: run `. ./export.sh` after setup. Do not run `python tools/idf.py` directly without the exported venv; it will fail with missing `rich_click`.
- Initial setup: `./install.sh` then `. ./export.sh`. Use `git submodule update --init --recursive` if you work with submodule-backed components.
- Non-GitHub forks must run `tools/set-submodules-to-github.sh` before submodule init (see README).

## Build & Flash

- Inside a project directory:
  - `idf.py set-target <chip>`
  - `idf.py build`
  - `idf.py -p PORT flash monitor`
- `idf.py flash` auto-rebuilds; `idf.py app` / `idf.py app-flash` build/flash only the app.
- Bulk/framework builds use `idf-build-apps`:
  - `idf-build-apps build -p <project> -t <target>`
  - `idf-build-apps build -p tools/test_apps/system/build_tests/full_build -t esp32`
  - Build-dir pattern is `build_<target>_<config>` by default (`.idf_build_apps.toml`).
- CI-style build of only test-related apps:
  - `idf-ci build run --target <target> -p components -p examples -p tools/test_apps --only-test-related`
  - Build system and config rules are already set in `.idf_build_apps.toml`; don't pass `--build-system cmake`.
- Generate a CI child pipeline locally:
  - `idf-ci gitlab build-child-pipeline -p components -p examples -p tools/test_apps --modified-files <files>`

## Tests

- Integration test scripts are named `pytest_*.py` (`pytest.ini`).
- Run target tests after building; pytest resolves build dirs in this order: `build_<target>_<config>`, `build_<target>`, `build_<config>`, `build`.
- Target test examples:
  - `pytest --target esp32 -m qemu --junitxml=XUNIT_RESULT.xml`
  - `pytest --target linux -m "not macos"`
  - Markers describe runner hardware/capabilities; see `pytest.ini` for the full list.
- Host/unit tests live alongside components/tools and usually run with `pytest_for_ut` (CI alias) or plain `pytest`:
  - `pytest_for_ut` is defined in `tools/ci/utils.sh`; it disables the `idf-ci` and `pytest_embedded` plugins.
  - Host test files are often named `test_*.py` rather than `pytest_*.py`, so they need direct file invocation or `pytest_for_ut`.
  - `cd tools/test_idf_py && pytest --noconftest test_idf_py.py`
  - `cd components/nvs_flash/nvs_partition_tool && pytest test_nvs_gen_check.py`
- Tools/system tests sometimes require extra tools installed (e.g. `qemu-xtensa`, `qemu-riscv32`, `cmake@3.22.1`) via `tools/idf_tools.py install ...`.

## Pre-commit & Style

- Install hooks once: `pip install pre-commit && pre-commit install-hooks`
- Run on changed files: `pre-commit run --files <files>`
- Run on a branch range like CI: `pre-commit run --from-ref <base> --to-ref <head> --show-diff-on-failure`
- Skip a hook if needed: `SKIP=cleanup-ignore-lists pre-commit run ...`
- C/C++ formatting: `tools/format.sh <files>`. The `astyle` version (3.4.7) and rules must stay in sync with `.pre-commit-config.yaml`.
- Python formatting/linting: `ruff format` and `ruff check --fix --show-fixes` (config in `ruff.toml`; line length 120, Python 3.10, single quotes).
- Python type comments/annotations: `tools/ci/check_type_comments.py` (run by the pre-commit `mypy-check` hook).
- CMake files: `cmakelint --linelength=120 --spaces=4 --filter=-whitespace/indent`
- Spelling: `codespell` (uses `.codespellrc`).
- Shell scripts: `shellcheck` on `install.sh` and `export.sh`.
- Every commit message is linted by `conventional-precommit-linter` (commit-msg hook).
- Copyright/SPDX headers are enforced by `check-copyright`; new files need a proper header.

## Generated Files

If you change the source files below, regenerate and commit the outputs:

- SoC caps Kconfig: `python tools/gen_soc_caps_kconfig/gen_soc_caps_kconfig.py -d 'components/soc/*/include/soc/' 'components/esp_rom/*/' 'components/spi_flash/*/'`
- eFuse tables: `idf.py efuse-common-table` or `components/efuse/efuse_table_gen.py -t <target> <csv>`
- Wi-Fi remote API: `python components/esp_wifi/remote/scripts/generate_and_check.py`
- Error code docs: `python tools/err_codes_extract.py --search-dirs components/ --output /tmp/err_codes.csv --validate` then `python tools/err_codes_to_rst.py --csv /tmp/err_codes.csv --output /tmp/esp_err_defs.inc`
- Build/test rule README tables: `python tools/ci/check_build_test_rules.py check-readmes`
- Test script rules: `python tools/ci/check_build_test_rules.py check-test-scripts examples/ tools/test_apps components`

## Repo Layout

- `components/` — ESP-IDF component libraries.
- `examples/` — example projects; build from here.
- `tools/test_apps/` — internal test/example apps used by CI.
- `tools/ci/` — CI scripts and shared Python packages.
- `tools/` — `idf.py`, `idf_tools.py`, generators, and auxiliary tooling.
- `docs/en/`, `docs/zh_CN/` — Sphinx documentation; requires `IDF_PATH` set and the `esp_docs` package.

## Important Defaults

- CI treats warnings as errors during builds (see `[local_runtime_envs]` in `.idf_ci.toml`).
- `.idf_build_apps.toml` defines config rules (`sdkconfig.ci=default`, `sdkconfig.ci.*=`, `=default`) and common components for dependency-driven builds; build dir pattern is `build_@t_@w` (`build_<target>_<config>`).
- `.idf_ci.toml` controls artifact upload rules and which apps are considered test-related.
- `pytest.ini` has many hardware/capability markers; don't assume a local run can satisfy `ethernet`, `wifi_router`, `jtag`, etc.
