# Stale retries can overwrite a card that has changed elsewhere

Confidence: confirmed control-flow defect; occurrence requires an intervening card change. Impact: a valid card can become rejected again, and a repair can preserve an incorrect balance.

## Status after issue 04

Partially addressed, still open. The writer now checks a fresh physical counter
against the saved baseline and target **before** any payload page write. A
counter ahead of the target or behind the baseline no longer causes the stale
overwrite described below. It also re-confirms the UID on each attempt.

The equal-counter case remains: another terminal's operation can leave a valid
payload with different monetary contents at the pending target's counter (or an
otherwise allowed baseline). The writer still needs fresh payload reconciliation
to distinguish that from its own interrupted transaction. Counter guards alone
do not establish transaction identity.

## Defect

After a failed transaction, [`write_failed()`](../../main/state_machine.c#L549) retains `data_to_write`. On another presentation it resumes `WRITE_CARD` when the UID matches, including for security failures. It does not compare the freshly read balance, deposit, or physical counter with the saved transaction's baseline and intended result.

Before issue 04, `write_card()` wrote pages 11–14 before checking the counter difference, so rejection came after overwriting the card. That ordering is now fixed. Checking identity and counter still does not establish that this card has the expected transaction history.

## Failure scenario

The original counter-ahead scenario (now blocked before mutation by issue 04):

1. Till A starts a transaction targeting counter 101, then enters `WRITE_FAILED` after an uncertain write outcome.
2. The customer takes the card to till B for repair or another transaction. B leaves it valid with physical counter 102 and an updated balance.
3. The card returns to A while its failed transaction remains pending. Matching the UID triggers the old transaction again.
4. A writes its old balance and payload counter 101, then notices that the physical counter is already 102 and returns failure. The card now fails counter verification until repaired again.

If the intervening operation leaves the physical counter equal to A's intended counter, the negative-difference check does not help: equal counters alone do not identify which operation committed. See also [counter-read and retry errors](04-counter-read-and-retry-errors.md).

## Effect of re-presentation recovery

Retaining the original target is useful and should be preserved: without an intervening operation, re-presentation normally completes the same payment rather than charging again. Removal does not discard the target, and another UID generally leaves it pending too. The defect is the missing check for changes to the original card while that target remains pending. Repeated presentation cannot resolve a physical counter already greater than the saved target.

## Proposed fix

Before any page write, authenticate the card and obtain a successful, fresh counter and payload read. Classify that state against both a trusted saved baseline and the intended result. A valid state reflecting an unrelated or newer operation must terminate the stale retry without altering the card.

Treat an operation as already committed only when the intended contents and available trusted transaction record support that conclusion; counter equality alone is insufficient. A recognized interrupted state may be completed using the saved intended values, subject to the recovery protocol described in [interrupted multipage writes](02-interrupted-multipage-writes.md).

Counter feasibility checks now precede payload mutation and use the retained baseline and target. Extend that preflight to validate payload contents as well. Unknown states should require explicit reconciliation, not overwrite or an automatic balance adjustment. To extend reconciliation across reboot, persist transaction identity, baseline, and intended values as described in issue 02; persistence is not required to fix the immediate stale-retry check.

## Verification

Simulate a failed A transaction followed by a successful B operation, then retry A. Assert that no page-write command occurs for a newer or unrelated valid state. Cover an unchanged baseline, a matching committed result, an equal-counter result with different contents, a partial write, failed fresh reads, and recovery after reboot. Verify that completing an already committed transaction does not charge or log it twice.
