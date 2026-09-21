# PN532 command acknowledgements are treated as completed card writes

## Defect

[`mfu_write_page()`](../../main/pn532.c#L1242) and
[`mfu_increment_counter()`](../../main/pn532.c#L1274) return success after
`pn532_send_cmd_check_ack()` receives the PN532 command ACK. They omit the
operation response: command receipt does not establish a completed card write.

The read and authentication helpers also omit a response-ready wait after the
ACK. [`pn532_read_data()`](../../main/pn532.c#L269) discards the I²C ready byte,
and the helpers omit frame-integrity checks. The IRQ queue and
[`waitready()`](../../main/pn532.c#L342) do not associate notifications with
particular responses.

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

## Proposed fix

Implement one complete PN532 exchange: transmit the command, validate its ACK,
wait for the operation response, read a complete frame, validate its integrity
and expected response command, then interpret operation status and any required
tag ACK/NAK. Propagate failures at every layer. Check the I²C ready indication
and bounds before accepting data.

Serialize exchanges through completion, consume notifications consistently,
and check actual readiness. Resynchronize after a timeout or malformed response
before issuing dependent commands.

Distinguish completion, rejection, and unknown outcome. Reconcile against the
same card and pending transaction before repeating increments; follow
[counter recovery](04-counter-read-and-retry-errors.md) and
[transaction retry rules](05-stale-transaction-retries.md).

## Verification

Inject delayed responses, card NAKs, missing responses after successful writes,
I²C failures, malformed frames, and stale IRQ events. Check that dependent
writes stop, invalid frames never count as success, and a completed increment
is not repeated after its response is lost. Confirm behavior with captured
PN532 traffic and controlled card removal.

Verify re-presentation: missed readback after a completed write
leaves physical counter equal to target, requiring zero increment and no second
charge; a partial write with the old physical counter requires only the pending
increment.

## Confidence and impact

The omitted result handling is definite. Its occurrence rate and timing effects
require hardware observation. This can leave a repairable inconsistent card;
it does not by itself demonstrate permanent EEPROM damage.
