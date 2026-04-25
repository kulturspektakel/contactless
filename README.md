# contactless-v3

ESP32-S3 firmware for a battery-powered contactless point-of-sale
terminal used at the Kulturspektakel festival. Customers carry a Mifare
Ultralight card with a stored balance; staff tap purchases on this
device, which writes the new balance back to the card and (optionally)
prints a paper receipt over Bluetooth.

## What it does

- Reads / writes Mifare Ultralight cards over a PN532 NFC reader
  (HMAC-SHA1 signature on every card to prevent forgery; balance and
  deposit fields).
- Connects to a thermal receipt printer over BLE (NimBLE central) and
  prints itemized receipts.
- Syncs the active product list and uploads transaction logs to
  `api.kulturspektakel.de` over HTTPS, gated by an HMAC-SHA1 device
  auth header.
- Drives a 128×64 SSD1309 OLED, a numeric keypad, a buzzer, and battery
  / USB-power management.
- Supports a "privileged" mode (entered by tapping a privilege card)
  for top-up, cashout, donation, repair, and crew-card enrollment.

## Hardware

- ESP32-S3 module, **no PSRAM**, 16 MB flash (GigaDevice).
- PN532 RFID/NFC reader on I²C.
- SSD1309 128×64 OLED on SPI (driven through u8g2).
- GTW HS6622S-class BLE thermal printer (matched by service UUID,
  not by name — see CLAUDE.md).
- 4×4 keypad, buzzer, battery + USB charge circuit.

## Build & flash

The toolchain is ESP-IDF v5.4 + the ESP-IDF VS Code extension.
Setup notes and the project's known footguns live in
[`CLAUDE.md`](./CLAUDE.md). In short:

1. Install ESP-IDF v5.4 and the VS Code extension.
2. Make sure `IDF_PATH` is set in the environment VS Code sees
   (`launchctl setenv IDF_PATH /path/to/esp-idf` on macOS, or launch
   VS Code from a shell where you've sourced `$IDF_PATH/export.sh`).
3. Open the project, set the USB port via the extension's status-bar
   port picker (or edit `.vscode/settings.json`'s `idf.port`).
4. Use the extension's *Build* and *Flash* commands. Flashing is over
   UART by default.

## Provisioning a fresh device

Two stores need values on a brand-new board:

### 1. eFUSE block 3 — `DEVICE_ID`

Defined in `main/esp_efuse_custom_table.csv`. 128 bits, used to
identify this device to the backend. Burned once with
`espefuse.py burn_block_data BLOCK3 …` (irreversible).

### 2. NVS namespace `device_config`

Set per device with `nvs_partition_gen.py` (or run a one-shot
provisioning sketch). All keys are strings unless noted.

| key             | meaning                                                  |
|-----------------|----------------------------------------------------------|
| `wifi_ssid`     | WiFi network the device joins                            |
| `wifi_password` | WiFi password                                            |
| `salt`          | 32-byte HMAC-SHA1 salt used for card signatures and the  |
|                 | HTTP auth header (must match the backend's salt)         |
| `product_list`  | int32, ID of the initially active product list           |
| `sound_mode`    | int, 0 = silent / 1 = beep                               |
| `order_counter` | int, last order number (auto-increments at runtime)      |

Names are defined in [`main/constants.h`](./main/constants.h).

The active product list and the suspended-crew-card and
privilege-token lists arrive from the backend at runtime via
`fetch_config` and live in `/littlefs/config.cfg` as protobuf.

## Repository layout

- `main/` — firmware sources (one task per `.c` file, mostly).
- `components/` — vendored components: nanopb, u8g2, esp-idf-mfrc522,
  esp-idf-lib.
- `partitions.csv` — `nvs` (12 KB) + `phy_init` (4 KB) + `factory`
  (1.4 MB) + `littlefs` (~14 MB). No OTA partition.
- `sdkconfig.defaults` — tracked Kconfig overrides; the comments in
  this file are load-bearing (some flags fix specific crashes — see
  CLAUDE.md before changing them).
- [`CLAUDE.md`](./CLAUDE.md) — operating manual for working on this
  codebase: gotchas, conventions, and the hard-won rules from past
  debugging sessions.
