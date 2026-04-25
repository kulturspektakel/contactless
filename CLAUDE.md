# contactless-v3

ESP32-S3 firmware for a battery-powered contactless point-of-sale terminal:
WiFi STA + NimBLE central (to a thermal receipt printer over GATT) +
PN532 RFID/NFC + SSD1309 OLED + littlefs + HTTPS.

ESP-IDF v5.4. No PSRAM. 16 MB flash (GigaDevice "gd"). DIO mode.

## Build / flash

The VS Code ESP-IDF extension is the only working build path on the
maintainer's machine — the shell `export.sh` is broken by a click/python
3.14 mismatch in the local IDF venv. Don't try `idf.py` from a terminal
without confirming the venv works first; ask the user to trigger the
extension's build/flash action instead.

Recommended VS Code extensions are pinned in `.vscode/extensions.json`:
the ESP-IDF extension and Microsoft's C/C++ extension pack (which
provides IntelliSense, CMake Tools, themes). **Do not also install
`llvm-vs-code-extensions.vscode-clangd`** — clangd and Microsoft's
cpptools fight over the same files and you get spurious "cannot open
source file" errors. `c_cpp_properties.json` reads
`build/compile_commands.json` for accurate per-file include resolution,
so IntelliSense matches the actual build.

`.vscode/settings.json` is committed but mostly generic — `idf.currentSetup`
reads `${env:IDF_PATH}`, so each developer just needs that one env var
set in VS Code's environment. On macOS, the simplest one-time setup is
`launchctl setenv IDF_PATH /path/to/esp-idf` and a VS Code restart; or
always launch VS Code from a shell where `export.sh` was sourced.
**Don't paste an absolute home-directory path back into
`idf.currentSetup`** — the env-var ref is what makes the file portable.
`idf.port` is the one per-machine value still checked in (single-
developer convenience); a second developer would need to override it
in their user-level settings.

When changing config, edit `sdkconfig.defaults` (tracked, with comments
explaining each non-default flag). The generated `sdkconfig` is gitignored
but **must be regenerated** for new defaults to apply — IDF's defaults-merge
only fills options that are missing from the existing `sdkconfig`, so a
stale file silently keeps old values. Verify with `grep CONFIG_FOO sdkconfig`
after any defaults change.

## Where things live

- `main/main.c` — task spawn order. State machine is `TASK_PRIO_HIGH`; the
  rest are `TASK_PRIO_NORMAL` and unpinned.
- `main/state_machine.c` / `state_machine.h` — central event loop driving the
  UI / payment flow. Event and mode enums live in `state_machine.h`; add new
  events/modes there.
- `main/printer.c` — NimBLE central; scan, connect, GATT-write to the
  thermal printer. Print queue + processing live here too.
- `main/fetch_config.c`, `main/log_uploader.c`, `main/log_writer.c`,
  `main/time_sync.c` — HTTPS to the backend, log uploads, NTP. All HTTP
  callers serialize via the `network_request` binary semaphore
  (`main/network_request.h`).
- `main/local_config.c` — loads/holds the active product list from
  `/littlefs/<config>` (protobuf). Decodes via nanopb in `components/nanopb/`.
- `main/http_auth_headers.c` — SHA-1-based device auth header attached to
  every outbound HTTPS request.
- `main/power_management.c` — battery + USB voltage sampling, power-off
  timer, low-battery handling.
- `main/event_group.h` — startup gating bits (incl. the `INITIAL_FETCH_DONE`
  gate referenced below).
- `main/rfid.c`, `main/pn532.c` — PN532 driver (I²C) and Mifare Ultralight
  tag handling.
- `main/display.c` — u8g2-based SSD1309 OLED rendering.
- `main/constants.h` — API host, NVS namespace/key names, task names, task
  priority constants, balance limits.
- `partitions.csv` — `nvs` (12 KB) + `phy_init` (4 KB) + `factory` app
  (1.4 MB) + `littlefs` (~14 MB). **No OTA partitions** — firmware updates
  go over the USB cable.

## Cross-task communication

Tasks talk via a small set of FreeRTOS primitives. When adding code,
reuse these rather than introducing new globals.

- `state_events` (queue) — every UI/transaction transition goes through
  `trigger_event(event_t)`; the state-machine task `xQueueReceive`s and
  dispatches via `process_event()`.
- `log_queue` (queue, owned by `log_writer.c`) — async log entries
  (transactions, enrollment events). `log_writer` encodes to protobuf and
  writes a `/littlefs/logs/XXXXXXXX.log` file, then nudges `log_uploader`.
- `print_queue` (queue, owned by `printer.c`) — async print jobs from
  `submit_print_job()`. The printer task drains it and issues GATT writes
  when connected.
- `config_update_queue` (queue, owned by `local_config.c`) — list-id
  changes from the menu.
- `network_request` (binary semaphore, declared in `main.c`, used in
  `fetch_config.c`, `time_sync.c`, `log_uploader.c`, `printer.c`) — the
  one knob that serializes radio-busy work.
- `event_group` (`event_group.h`) — startup gating bits and "needs redraw"
  signal to the display task.
- Direct task notifications via `xTaskGetHandle(TASK_NAME)` +
  `xTaskNotifyGive` — used to wake `fetch_config` after a config-update
  request, etc.

**Convention: `xQueueSendFromISR` is used from regular task context too**
(e.g. inside `write_log`, `submit_print_job`). This is intentional —
callers may be running inside the state machine's `taskENTER_CRITICAL`
section where the plain `xQueueSend` would assert. Keep using the
`FromISR` variants when sending to `log_queue`, `print_queue`,
`config_update_queue` regardless of whether you're actually in an ISR.

## Hard rules — these are non-obvious and have all bitten us

### Logging in hot paths

`ESP_LOGI` / `ESP_LOGE` are not crash-safe everywhere. The macro reads
its format string from flash-mapped `.rodata` and acquires a FreeRTOS
mutex; both fail in two specific situations on this board:

1. **Flash cache is briefly disabled** during BLE radio activation,
   PHY/coex calibration, NVS commits, and other flash writes. Anything
   that does a flash read in that window faults
   ("Cache disabled but cached memory region accessed"). The format
   string itself is a flash read, so even a `ESP_LOGI("foo")` with no
   args can crash. We've hit this in BLE GAP/GATT callbacks, in
   `schedule_reconnect` / `stop_bluetooth` (called from those callbacks),
   and immediately after `ble_gap_disc` / `ble_gap_connect`.
2. **Interrupts are disabled** inside a `taskENTER_CRITICAL` section.
   The log mutex can't be acquired with interrupts off — this asserts
   or deadlocks. The state-machine event-dispatch loop already calls
   this out with a `// log needs to be outside of critical section`
   comment.

Rule: in BLE callbacks, in code reachable from BLE callbacks
(`schedule_reconnect`, `stop_bluetooth`, `gatt_*_cb`), and inside
`taskENTER_CRITICAL`, **do not log**. Mutate state and
`xTaskNotifyGive` / queue-send to a consumer task that logs from a
quieter context. `ESP_LOGE` on rare init-failure paths is acceptable —
those flows are slow enough that the cache-disable window has closed
and aren't usually inside critical sections.

This rule has cost us several debugging cycles already, including a
crash on a bare `ESP_LOGI("Disconnected")` inside the
`BLE_GAP_EVENT_DISCONNECT` handler. Don't re-add such logs for
cosmetic reasons.

### Flash cache / NVS / TLS

The chip is no-PSRAM and the GD flash chip's auto-suspend doesn't fully
cover every NVS write path. Several Kconfig flags **must** stay off because
their NVS commits open dcache-disable windows that fault concurrent flash
reads (mbedtls .rodata tables, NimBLE host code, ESP_LOGI format strings):

- `CONFIG_ESP_WIFI_NVS_ENABLED=n` — without this, the post-WiFi-connect
  NVS commit faults the first TLS handshake (mbedtls EC curve / X.509 OID
  table reads on the other core). Boot log must show
  `wifi:config NVS flash: disabled`.
- `CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE=n` — without this, the
  PHY NVS write that fires on the first BLE radio activation faults
  NimBLE's own `ble_gap_disc_report` while it processes the first advert.

Defense-in-depth (also kept in `sdkconfig.defaults`):
`CONFIG_SPI_FLASH_AUTO_SUSPEND=y`, `CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=y`.

### NimBLE re-init is broken

Do not call `nimble_port_deinit()` followed by `nimble_port_init()`.
On IDF v5.4 the second `nimble_port_init` deadlocks in `vListInsert` and
the interrupt watchdog kills the device. `stop_bluetooth()` must only
cancel the active scan; leave the stack initialized for the device's
whole runtime. The BLE controller modem-sleeps on its own when idle.

### Identify the BLE printer by service UUID

The GTW HS6622S thermal printer (and family) doesn't put its name in the
primary advertisement — the name is in the SCAN_RSP packet, which is
unreliable under WiFi+BT coex pressure. `gap_event_cb` matches on either
of two advertised service UUIDs:

- `0x18F0` (16-bit, GTW custom)
- `49535343-FE7D-4AE5-8FA9-9FAFD205E455` (128-bit, ISSC UART)

Both are model-level identifiers, so the same code works for any printer
of this family. Do not hardcode BD addresses.

### TLS vs BLE serialization

Two pieces work together:
1. `INITIAL_FETCH_DONE` event bit (`event_group.h`) gates `nimble_port_init()`
   in `main/printer.c` until `fetch_config` has had its first attempt
   (30 s timeout falls open for offline boots).
2. The printer task takes the `network_request` semaphore around every
   BLE scan/connect cycle, so a periodic `fetch_config` waking up mid-scan
   waits for the radio to be free (~11 s worst case) instead of crashing.

Both are required; removing either re-introduces the original cache-disable
race when WiFi reconnects mid-session.

### Power

The device runs on battery. The `USB ~2100mV` line in boot logs is the
charger input voltage, not a sign of wall power — the `battery ~2100mV`
reading next to it is the real power source. Do not suggest `WIFI_PS_NONE`
or other "always-on" workarounds. IDF defaults (`WIFI_PS_MIN_MODEM`,
dynamic frequency scaling, NimBLE default scan/connect intervals) are
the baseline.

## Kult-epoch (crew card `valid_until`)

Crew cards store their expiry as a 16-bit "days since Kult-epoch" count,
where the epoch is **2025-01-01 (UTC)**. Encoding lives in
`days_since_kult_epoch()` in `main/state_machine.c`; decoding (and display
formatting) lives in `valid_until_date()` in `main/display.c` (it builds a
`struct tm` for 2025-01-01, adds `valid_until` days, calls `mktime`).

The non-obvious wrinkle: `days_since_kult_epoch()` subtracts 1 if the
current UTC hour is `< 4`. That nudges the day-boundary roughly to the
end of the festival night in Berlin time — a card validated/charged
shortly after midnight local still counts as the previous festival day.
Anything reading or writing this field has to keep that offset in mind;
the 16-bit width gives ~179 years of headroom from 2025, so wraparound
isn't a concern.

The `valid_until` value is also part of the per-card signature
(`main/rfid.c`, `OFFSET_VAILD_UNTIL`/`LENGTH_VAILD_UNTIL`), so changing
how it's computed breaks signature verification on existing cards.

## Protobuf / nanopb

`.proto` files live in `components/nanopb/` (`logmessage.proto`,
`configs.proto`, `config.proto`, `product.proto`); the generated
`.pb.h` / `.pb.c` are checked in. `LogMessage` (uploaded transactions)
and `DeviceConfig` / `AllLists` (server config) are the message types
that touch most of the code. Encode/decode goes through
`pb_encode` / `pb_decode` with `pb_callback_t` for variable-length
fields like `crew_card_id`. If you add a field, regenerate the
`.pb.h` / `.pb.c` and commit them — there is no auto-generation step
in the build.

## Conventions

- Prefer editing existing files over creating new ones; the project is
  small and most logic belongs in an existing `main/*.c`.
- Comments only when the *why* is non-obvious — most of the comments in
  `sdkconfig.defaults` and around the BLE-callback code are exactly that
  kind. Don't strip them.
- Don't add backwards-compat shims; this is a single-firmware codebase.
- Match existing memory discipline: heap-allocate variable-sized log
  messages with `pvPortMalloc`, free on the consumer side. The state
  machine uses `taskENTER_CRITICAL` for transitions; new transition
  code should do the same.
