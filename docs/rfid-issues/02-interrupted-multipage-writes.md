# Interrupted writes need the pending transaction to finish recovery

Status: confirmed temporary-inconsistency window, with existing same-terminal recovery. Recovery after cancelling or losing the pending transaction is more limited. Individual-page tearing has not been reproduced in this investigation.

## Defect

[write_card()](../../main/rfid.c#L303) modifies one copy of the payload in place, then increments the hardware counter. The operations are separate:

| Order | Location | Contents |
| --- | --- | --- |
| 1 | Page 11 | Last UID byte and both encoded counter bytes |
| 2 | Page 12 | Deposit and balance |
| 3 | Pages 13 and 14 | Signature and terminator |
| 4 | Hardware counter 0 | Transaction counter increment |

These operations are not atomic, but the terminal does preserve both baseline and intended values in RAM: [`data_before_write` and `data_to_write`](../../main/state_machine.c#L382). Normal [`WRITE_FAILED`](../../main/state_machine.c#L549) retains that target when the card is removed. Presenting the same UID resumes writing it, including when the read reports a signature/counter mismatch or invalid monetary values. The amount is not subtracted again.

The [three-attempt loop](../../main/rfid.c#L521) applies per presentation. Subsequent presentations can each receive another three attempts; `WRITE_FAILED` imposes no overall retry limit. The UI explicitly requests removal and another attempt ([display.c](../../main/display.c#L1008)). The gap is loss or abandonment of this pending context, not absence of any recovery mechanism.

## Failure scenario

A payment starts at physical counter N and saves target N+1. Removal before the counter increment can leave mixed pages or a complete target payload with physical counter N. On the same pending terminal, the next read supplies the physical counter and the retry rewrites the saved target; the writer calculates an increment of 1. If the original increment already completed, the physical counter is N+1 and the calculated difference is 0 ([rfid.c](../../main/rfid.c#L322)). This avoids intentionally advancing the transaction again, subject to the [counter-read defects](04-counter-read-and-retry-errors.md).

Even out-of-range monetary values need not block this retry: [`read_card()`](../../main/rfid.c#L171) updates the UID and physical counter before returning `INVALID`, which the pending retry accepts. Deposit and balance share page 12, so interruptions between intact page writes retain their complete old or new values; intra-page damage is a separate possibility.

The card instead needs a fresh recovery path if the operator [cancels with D](../../main/state_machine.c#L573), power/reboot loses RAM, or it moves to another terminal. Cancellation abandons the pending workflow without immediately clearing the target bytes. Fresh payment rejects inconsistent data, and [fresh repair cannot recover every invalid payload](06-repair-rejects-damaged-payloads.md).

## Proposed fix

First fix the [signature buffer overflow](01-signature-buffer-overflow.md), [transport results](03-pn532-operation-results.md), [counter reads/retries](04-counter-read-and-retry-errors.md), and [stale retries](05-stale-transaction-retries.md). Preserve and verify the existing same-pending-transaction recovery before considering a format redesign.

If recovery must survive cancellation/reboot, persist a trusted transaction record before mutation containing UID, verified baseline, intended result, and transaction identity. Reconcile actual card state before resuming and record completion without duplicate accounting. Recovery on another terminal additionally needs trusted record sharing. This journal extends the guarantee; it is not a prerequisite for today's RAM-backed retry.

A card format with independently verifiable records is another option if recovery must be self-contained. Establish actual capacity and a recoverable commit protocol first. Neither reordered writes nor extra delays eliminate every interruption window.

## Verification

First demonstrate successful same-terminal re-presentation after each interrupted stage, including physical N, physical N+1, signature mismatch, and out-of-range monetary bytes. Verify the saved amount is applied once across multiple presentations. Then cover failed reads, stale targets, cancellation, reboot, and another terminal; require explicit reconciliation where context is unavailable. Test accounting/log completion as well as card acceptance.
