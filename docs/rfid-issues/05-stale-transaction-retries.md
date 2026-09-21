# Stale retries can overwrite a card that has changed elsewhere

Confidence: confirmed original defect requiring an intervening card change. Its remaining same-counter payload overwrite is now guarded; physical-reader validation is outstanding.

## Status

Implemented for normal regular-card transactions, including privileged repair.
[Issue 04](04-counter-read-and-retry-errors.md) already rejected unexplained
counters before mutation. The writer now reconciles fresh raw payload bytes
against immutable original and intended images as well. Unknown damage fails
closed; initialization remains a separate path.

## Original defect

After failure, [`write_failed()`](../../main/state_machine.c#L549) retains the
intended transaction and resumes `WRITE_CARD` for the same UID. Originally, it
could overwrite intervening changes before noticing an advanced physical
counter. Issue 04 fixed that ordering, but equal counters still permitted
overwriting a different transaction's monetary contents.

## Historical failure scenario

1. Till A starts a transaction targeting counter 101, then enters `WRITE_FAILED` after an uncertain write outcome.
2. Till B performs another operation, leaving a valid payload at counter 101 with different monetary values.
3. The card returns to A. Its UID and counter pass the issue 04 guards, so A could overwrite B's contents with its saved target.

The original counter-102 variant is already blocked by issue 04 before mutation.
The equal-counter variant is now blocked by the raw-payload checks below.

## Implemented fix

[`read_card()`](../../main/rfid.c#L110) captures exact pages 8–14 before Base64
normalization. This [read metadata](../../main/state_machine.h#L96) travels with
`current_card`; existing transaction creation copies it into `data_before_write`.
Later reads update `current_card`, never the saved original image. Missing raw
metadata prevents a regular write.

[`write_card()`](../../main/rfid.c#L394) retains UID, authentication, and counter
guards. It builds the intended image by copying the original 28 bytes and
overlaying only mutable pages 11–14 with the encoded target. Pages 8–10 must
remain exactly as observed originally. A fresh raw read is classified before
any payload write or counter increment:

| Physical counter | Accepted payload | Action |
| --- | --- | --- |
| Original | Exact original or ordered intended-page prefix followed by original pages | Complete the saved payload and increment once. |
| Original | Exact intended image | Increment once; do not rewrite pages. |
| Target | Exact intended image only | No page writes or increment; perform final verification. |

Before admitting a mixed image, the classifier rejects a different
self-consistently signed state, using its encoded counter and selected UID.
The exact originally authorized image is accepted first: privileged repair
therefore retains its original bad-signature or mismatched-counter baseline.

Unknown intra-page tears, out-of-order mixtures, changed untouched bytes, and
unrelated contents require explicit reconciliation. The existing `WRITE_FAILED`
remove/retry UI remains pending; rejection does not automatically cancel or
reconcile the transaction. Whole-page interruption recovery remains available,
but arbitrary range-invalid bytes are no longer blindly overwritten.

## Limits

Snapshots remain RAM-only. Cancellation abandons the workflow, reboot loses it,
and a fresh transaction captures a new baseline. Durable recovery is separate
[issue 02](02-interrupted-multipage-writes.md). A byte-identical target produced
by another terminal cannot be attributed without transaction identity or a
shared record. Read/compare/write is not atomic against another reader.

Recognizing an already-written target avoids card mutation and then follows the
existing success event path. This does not establish cross-terminal exactly-once
accounting or logging deduplication across crashes.

## Verification

The [RFID suite](../../tests/rfid/README.md) passes 70 cases and the
[PN532 suite](../../tests/pn532/README.md) passes 78 under ASan/UBSan. RFID cases
cover ordered boundaries, out-of-order mixtures, unrelated signed contents,
failed raw reads, original damaged repair snapshots, and complete-target paths
with no page rewrites. A deliberate test-hash collision exercises signed-mixture
rejection; the test hash does not validate SHA-1 compatibility.

ESP32-S3 compilation of the `rfid.c` and `state_machine.c` translation units
passes with existing warnings; this is not a full firmware build. The
state-machine event consumer and logging are not integrated into these tests.
Hardware timing, physical tearing, task concurrency, transaction lifecycle,
and accounting deduplication still need integration/hardware validation.
