# Failed counter reads can cause extra increments or destructive retries

## Defect

[`pn532_read_data()`](../../main/pn532.c#L269) clears the caller's buffer before
performing I²C and returns `false` when I²C fails.
[`mfu_read_counter()`](../../main/pn532.c#L1302) ignores that return value,
accepts the cleared status byte as success, and outputs counter zero.

The [retry loop](../../main/rfid.c#L523) also ignores counter-read failure,
allowing a stale counter to persist. [`write_card()`](../../main/rfid.c#L314)
overwrites payload pages before rejecting counter differences outside 0–3.

## Failure scenario

Consider a card with physical counter 2 and a pending transaction targeting 3:

1. The first attempt writes the payload and increments to 3, but verification
   fails.
2. The retry's counter read suffers an I²C failure and falsely returns zero.
3. The retry writes payload 3 and increments the physical counter by three,
   advancing it to 6.
4. Verification detects payload 3 versus physical 6; accurate retries then
   reject the negative difference.

For targets above three, a false zero instead triggers rejection after payload
mutation. An explicit read failure can also cause an extra increment if the
retained counter predates a completed attempt.

## Existing recovery

There are three immediate attempts, followed by further batches when the same
card is removed and presented again in
[`WRITE_FAILED`](../../main/state_machine.c#L549). Ordinary-write retries retain
the saved target. An accurate reread can correct a stale cached counter and
recover a transient failure. It cannot undo an extra physical increment.

If hardware reaches 6 while the target remains 3, unlimited re-presentations
cannot decrement it. The code rewrites payload 3 before rejecting the negative
difference each time. This needs a separately authorized repair using the
actual counter and trustworthy transaction history, not necessarily card
replacement.

## Proposed fix

Propagate transport and frame errors; update outputs only after successful
validation. Every caller must check success rather than reuse stale outputs.

Before mutation, read a fresh counter for the confirmed UID and reconcile it
against the immutable pending transaction's verified baseline and target.

Finish already-completed transactions without incrementing. Resume only
interrupted operations explained by that verified transaction. Reject ahead,
unexplained, unreadable, or changed-UID states before writing; never adopt a new
counter baseline for an old balance target.

See [operation outcomes](03-pn532-operation-results.md),
[interrupted writes](02-interrupted-multipage-writes.md), and
[stale transactions](05-stale-transaction-retries.md). Lost increment responses
remain unknown outcomes until reconciliation.

## Verification

Inject retry-read failures after completed increments, including false-zero
and stale-output cases. Test counter-ahead states and UID changes; require
rejection before mutation.

Verify successful re-presentation too: full success followed by missed
readback leaves physical counter equal to target, so the retry uses zero
increment and does not charge twice. A partial write with the old counter
finishes the saved payload and increments once. Repeat presentations must
preserve those results.

## Confidence and impact

The ignored returns, false-zero path, and validation ordering are definite.
They can produce persistent counter/payload disagreement requiring repair;
they do not establish irreversible physical card damage.
