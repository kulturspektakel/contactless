# PN532 command acknowledgements are treated as completed card writes

## Status

Implemented in the driver. Host fault-injection tests pass; physical reader/card
validation is still required. The application-level counter retry defect in
[issue 04](04-counter-read-and-retry-errors.md) remains open.

## Defect

[`mfu_write_page()`](../../main/pn532.c#L1242) and
[`mfu_increment_counter()`](../../main/pn532.c#L1274) previously returned success after
`pn532_send_cmd_check_ack()` received the PN532 command ACK. They omitted the
operation response: command receipt does not establish a completed card write.

The read and authentication helpers also omitted a response-ready wait after the
ACK. `pn532_read_data()` discarded the I²C ready byte, and the helpers omitted
frame-integrity checks. `waitready()` trusted queued IRQ edges rather than the
actual ready level, so stale or missed notifications could mislead it.

The [NXP PN532 manual](https://www.nxp.com/docs/en/user-guide/141520.pdf),
pages 36–44, specifies separate acknowledgements and responses. A new command
can abort an unfinished operation; bytes following a NOT READY I²C status are
invalid. Fixed sleeps do not prove completion.

## Failure scenario

A payload page write fails because the card moves or rejects the operation.
The driver nevertheless reports success, allowing later pages and the physical
counter to be updated. The resulting mixed payload fails its signature or
counter check. Final readback may detect this, but cannot restore the previous
contents if the card has left; see
[interrupted writes](02-interrupted-multipage-writes.md).

If an operation takes longer than the fixed delay, the following command can
interrupt it. A lost response can also hide a write that actually completed.
Therefore a transport error means the outcome is unknown, not that the card
remained unchanged.

## Existing recovery

The [RFID loop](../../main/rfid.c#L522) makes three immediate attempts. After
failure, removing and presenting the same card again can start another batch
through [`write_failed()`](../../main/state_machine.c#L549). For ordinary writes,
the saved target remains unchanged. A transient failure can therefore recover
without repair when communications stabilize. Repeated false results, or an
extra physical counter increment, can defeat this recovery; exhausting one
batch alone does not make the card permanently unusable.

## Implemented fix

All operational helpers now use a synchronous command/ACK/response exchange in
[`main/pn532.c`](../../main/pn532.c), including startup, selection, deselection,
polling and legacy card helpers. The RFID task remains the sole driver owner.

- Check I²C construction/execution results and the ready status byte. Failed
  reads leave caller outputs unchanged.
- Treat queued interrupts as wake-up hints; inspect the GPIO level and use
  bounded waits to recover from missed edges.
- Consume the ACK separately, then wait for the result. Read each result in
  one I²C transaction; validate preamble, bounded length, length checksum,
  direction, expected response command, data checksum and postamble. Unsupported
  oversized/extended frames fail closed.
- Validate operation-specific lengths and status before accepting page data,
  PACK, counters or write success. `InDataExchange` interprets page-write tag
  ACK/NAK and reports its status.
- After an exchange failure, require a raw host ACK abort followed by a valid
  `GetFirmwareVersion` exchange before sending another requested command. Failed
  resynchronization blocks that command. It does not reset/reselect the reader,
  replay the failed mutation, or establish that the card remained unchanged.

Counter increments additionally read the full 24-bit counter before and after
the command and require the expected delta. A zero delta performs a validated
read without issuing `INCR_CNT`. Its four-bit tag ACK has no CRC, so the raw
`InCommunicateThru` result can report CRC error (`0x02`) even when the tag changed.
That status is allowed only to proceed to read-back, never as proof of success.
An explicit NAK, another error, or a missing/malformed response returns failure.
See the [Ultralight EV1 datasheet](https://www.nxp.com/docs/en/data-sheet/MF0ULX1.pdf)
and [NXP's explanation of raw commands with short ACKs](https://community.nxp.com/t5/NFC/NTAG-I-C-Sector-Select-on-PN532/td-p/505506).

The existing boolean API is retained: `true` means confirmed success; `false`
includes both rejection and unknown outcome. It must never be interpreted as
proof that retrying a mutation is safe. The driver verifies the **requested
delta**, not the application's immutable transaction target. If the application
ignores a failed counter refresh and requests the wrong delta, the driver can
still execute it correctly and overshoot that target. Reconciliation and
pre-mutation checks remain [issue 04](04-counter-read-and-retry-errors.md) and
[issue 05](05-stale-transaction-retries.md); the 16-bit application format remains
[issue 08](08-counter-overflow.md).

## Verification

[`tests/pn532`](../../tests/pn532/README.md) compiles the actual driver against
scripted I²C/GPIO/queue mocks under AddressSanitizer and UndefinedBehaviorSanitizer.
It covers delayed/missing responses, NAKs, I²C failures, invalid frames, stale or
missing IRQ notifications, output preservation, counter read-back and failure
paths, and the resynchronization barrier. The driver never internally repeats an
increment after losing its result. ESP32-S3 compilation of `pn532.c` also passes.

Hardware validation remains outstanding: capture PN532 traffic, verify the
four-bit counter ACK behavior and resynchronization timing, and remove cards
during writes. These host tests do not exercise the application state machine
or prove that its issue 04 retry path is safe.

Also verify re-presentation on hardware: missed readback after a completed write
leaves physical counter equal to target, requiring zero increment and no second
charge; a partial write with the old physical counter requires only the pending
increment.

## Confidence and impact

The omitted result handling is definite. Its occurrence rate and timing effects
require hardware observation. This can leave a repairable inconsistent card;
it does not by itself demonstrate permanent EEPROM damage.
