# Interrupted writes need the pending transaction to finish recovery

Status: confirmed temporary-inconsistency window, with same-terminal recovery for recognized whole-page interruptions. Issue 05 now rejects unknown tears even while a transaction remains pending. Recovery after cancelling or losing that transaction is more limited. Individual-page tearing has not been reproduced in this investigation.

## Defect

[write_card()](../../main/rfid.c#L394) modifies one copy of the payload in place, then increments the hardware counter. The operations are separate:

| Order | Location | Contents |
| --- | --- | --- |
| 1 | Page 11 | Last UID byte and both encoded counter bytes |
| 2 | Page 12 | Deposit and balance |
| 3 | Pages 13 and 14 | Signature and terminator |
| 4 | Hardware counter 0 | Transaction counter increment |

These operations are not atomic, but the terminal preserves the original raw image and intended values in RAM: [`data_before_write` and `data_to_write`](../../main/state_machine.c#L382). Normal [`WRITE_FAILED`](../../main/state_machine.c#L549) retains them when the card is removed. Presenting the same UID resumes the attempt, including for signature/counter mismatch or invalid-value events. The writer then validates fresh bytes against those snapshots; accepting the event does not authorize arbitrary repair. The amount is not subtracted again.

The [three-attempt loop](../../main/rfid.c#L668) applies per presentation. Subsequent presentations can each receive another three attempts; `WRITE_FAILED` imposes no overall retry limit. The UI requests removal and another attempt ([display.c](../../main/display.c#L1008)). Rejected unknown states remain pending, not automatically cancelled or reconciled.

## Failure scenario

A payment starts at physical counter N and saves target N+1. Removal before increment can leave an ordered prefix of completed target pages followed by original pages. At N, the same pending terminal accepts that recognized interruption and finishes the saved payload, then increments once. A complete target payload at N needs only its increment. At N+1, only the exact target image is accepted, with no page rewrites or increment. These are the implemented [counter](04-counter-read-and-retry-errors.md) and [raw-payload](05-stale-transaction-retries.md) safeguards.

Before issue 05, range-invalid monetary bytes could be overwritten using the retained target. Now unknown intra-page tears and out-of-order mixtures require explicit reconciliation; a different self-consistently signed state is rejected before admitting a mixture. Deposit and balance share page 12, so interruptions between intact page writes retain their complete old or new monetary values. Original bad-signature/counter images explicitly authorized for repair remain accepted baselines.

The card instead needs a fresh recovery path if the operator [cancels with D](../../main/state_machine.c#L573), power/reboot loses RAM, or it moves to another terminal. Cancellation abandons the pending workflow without immediately clearing the target bytes. Fresh payment rejects inconsistent data, and [fresh repair cannot recover every invalid payload](06-repair-rejects-damaged-payloads.md).

## Proposed fix

The [signature](01-signature-buffer-overflow.md), [transport](03-pn532-operation-results.md), [counter](04-counter-read-and-retry-errors.md), and [stale-retry](05-stale-transaction-retries.md) fixes are implemented. Validate this guarded recovery and address [intentional sleep](07-sleep-during-card-writes.md) next, before a format redesign.

If recovery must survive cancellation/reboot, persist a trusted transaction record before mutation containing UID, verified baseline, intended result, and transaction identity. Reconcile actual card state before resuming and record completion without duplicate accounting. Recovery on another terminal additionally needs trusted record sharing. This journal extends the guarantee; it is not a prerequisite for today's RAM-backed retry.

A card format with independently verifiable records is another option if recovery must be self-contained. Establish actual capacity and a recoverable commit protocol first. Neither reordered writes nor extra delays eliminate every interruption window.

## Verification

Demonstrate positive recovery at each ordered whole-page boundary, complete targets at N/N+1, and authorized damaged repair baselines. Require no mutation for unknown tears, unrelated signed states, or failed reads. The 70-case RFID host suite covers these byte/counter checks; it does not integrate the state-machine event consumer or logging. Test cancellation, reboot, other terminals, and exactly-once accounting separately. No journal is required for recognized RAM-backed recovery, but extending recovery requires trusted evidence.
