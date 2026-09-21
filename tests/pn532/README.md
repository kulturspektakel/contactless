# PN532 host regression tests

Run from the repository root with Clang available:

```sh
sh tests/pn532/run.sh
```

The script compiles the actual `main/pn532.c` implementation with AddressSanitizer
and UndefinedBehaviorSanitizer. SDK stubs replace only I²C, GPIO, FreeRTOS, and
logging. The binary is built in a temporary directory and removed afterward;
the ESP-IDF build and generated configuration are not used or modified.

The scripted device checks complete outgoing command bytes and incoming read
sizes: seven bytes for I²C status plus ACK, and 65 for status plus a bounded
response. It models delayed results, stale/missing IRQ events, tick wrap, partial
I²C failures, rejected operations, invalid frames, and recovery exchanges.

Cases verify output preservation, full 24-bit increment readback, CRC-error
handling for the tag's four-bit ACK, no automatic increment replay, and the
abort/firmware-response barrier before later commands. Every case fails on an
unexpected command, including an unintended extra write or increment.

Startup, firmware, SAM configuration, deselection, one/two-target polling,
four/seven-byte UID selection, and legacy Mifare Classic read/write fixtures
also check exact response lengths and preservation of outputs on rejection.

These tests establish driver behavior against the supplied wire fixtures. They
do not prove RF timing, real IRQ electrical behavior, tag EEPROM atomicity, or
recovery after physical card removal. Hardware testing remains necessary.
