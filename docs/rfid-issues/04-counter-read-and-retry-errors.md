# Failed counter reads can cause extra increments or destructive retries

## Status

Implemented for normal regular-card writes, including payment, top-up,
cash-out/donation and privileged repair. Initialization remains a separate path.
Hardware fault-injection and re-presentation validation are still required.

The [issue 03 driver fix](03-pn532-operation-results.md) removed false-zero reads.
This fix removes the ignored retry read and stale-delta calculation, moves
counter validation before page writes, and binds the increment to the expected
physical counter. Full payload reconciliation for a different transaction at the
same counter remains [issue 05](05-stale-transaction-retries.md).

## Defect

Before issue 03 was fixed, `pn532_read_data()` cleared the caller's buffer before
performing I²C and returned `false` when I²C failed. `mfu_read_counter()` ignored
that return value, accepted the cleared status byte as success, and output
counter zero.

The original retry loop also ignored counter-read failure, allowing a stale
counter to persist. `write_card()` overwrote payload pages before rejecting
counter differences outside 0–3.

## Failure scenario

The original false-zero scenario (before the driver fix) involved a card with
physical counter 2 and a pending transaction targeting 3:

1. The first attempt writes the payload and increments to 3, but verification
   fails.
2. The retry's counter read suffers an I²C failure and falsely returns zero.
3. The retry writes payload 3 and increments the physical counter by three,
   advancing it to 6.
4. Verification detects payload 3 versus physical 6; accurate retries then
   reject the negative difference.

For targets above three, a false zero instead triggered rejection after payload
mutation. After issue 03, the stale-output variant remained: an increment could
complete, its response or readback could fail, and a failed retry refresh could
leave cached N. The application would request another increment of one; the
driver would correctly confirm N+1 to N+2 while the saved target was only N+1.

## Existing recovery

There are three immediate attempts, followed by further batches when the same
card is removed and presented again in
[`WRITE_FAILED`](../../main/state_machine.c#L549). Ordinary-write retries retain
the saved target. An accurate reread can correct a stale cached counter and
recover a transient failure. It cannot undo an extra physical increment.

If hardware has already reached 6 while the target remains 3, unlimited
re-presentations cannot decrement it. The fixed writer now refuses that state
without overwriting the payload. It still needs a separately authorized repair
using the actual counter and trustworthy transaction history, not necessarily
card replacement.

## Implemented fix

The writer in [`main/rfid.c`](../../main/rfid.c) snapshots `data_before_write`
and `data_to_write` for the attempt batch. Readback may update `current_card`,
but never becomes a replacement transaction baseline.

For every normal regular-card attempt:

1. Require matching baseline/target UIDs and types, and target counter exactly
   baseline + 1 using widened arithmetic. Wrapped or malformed targets fail
   before any card mutation.
2. Re-select the card with a bounded timeout and verify its UID, then
   authenticate. A replaced card is rejected before authentication or writing.
3. Successfully read the fresh physical counter. Accept only the saved baseline
   or target. Failed reads and counters behind/ahead of those states stop before
   writing any payload page.
4. Write the same saved payload. At the baseline, request exactly one increment;
   at the target, issue no increment. This permits both partially written and
already-incremented transactions to finish without a second charge.
5. The driver rereads the full physical counter immediately before incrementing
   and requires it to equal the caller's expected value. A change since preflight
   stops the increment; it cannot silently become a new baseline. Readback must
   equal that expected value plus the requested delta.
6. The final verified card must match the target's UID, type, counter and monetary
   values before reporting success.

The public 16-bit counter read now rejects values above 65,535 instead of
truncating them into an apparently valid baseline. This also guards the mutation
hazards in [issue 08](08-counter-overflow.md), but does not provide an exhaustion
UI/replacement workflow or change the card format.

Privileged repair still accepts its explicitly authorized baseline even when the
old payload signature is invalid. Crew cards do not use the regular-card counter
guards, and initialization does not require a normal-payment baseline.

Counter equality is not proof of transaction identity. This fix rejects newer or
unexplained **counter** states; comparing a valid same-counter payload against
the saved monetary contents remains issue 05. Interrupted multi-page writes,
loss of the pending RAM record, and physical card removal remain possible.
The driver's read/compare/increment sequence is not an atomic operation against
another reader; it prevents this terminal from knowingly incrementing a changed
baseline, not arbitrary concurrent card modification.

See [operation outcomes](03-pn532-operation-results.md),
[interrupted writes](02-interrupted-multipage-writes.md), and
[stale transactions](05-stale-transaction-retries.md). Lost increment responses
remain unknown outcomes until reconciliation.

## Verification

Host regression suites are in [`tests/rfid`](../../tests/rfid/README.md) and
[`tests/pn532`](../../tests/pn532/README.md). They exercise the actual RFID writer
and retry loop with a mock tag, and the actual PN532 driver with scripted wire
responses respectively. Test doubles do not establish real RF behavior or
replace a full firmware/hardware test.

Both suites pass under AddressSanitizer and UndefinedBehaviorSanitizer.
`rfid.c` and `pn532.c` also compile with the recorded ESP32-S3 compiler commands;
only the existing state-structure and IRQ pointer-cast warnings remain.

Cover failed preflight reads, stale cached counters, changed UIDs, behind/ahead
counters, invalid/wrapped targets, a counter changed immediately before increment,
lost increment results, and successful partial-write/already-incremented retries.
Require zero page writes when preflight fails and no duplicate increment after
an uncertain outcome. Exercise 65,534, 65,535 and unrepresentable physical values.

Verify successful re-presentation too: full success followed by missed
readback leaves physical counter equal to target, so the retry uses zero
increment and does not charge twice. A partial write with the old counter
finishes the saved payload and increments once. Repeat presentations must
preserve those results.

## Confidence and impact

The original false-zero path, ignored retry return and late validation were
definite defects. The changes address these counter paths; their role in a
particular field incident and real reader timing still require hardware evidence.
Previously damaged cards may still need repair. This does not establish
irreversible physical card damage.
