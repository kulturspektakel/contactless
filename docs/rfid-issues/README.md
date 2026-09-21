# RFID write and recovery issues

These documents record the investigation into cards becoming unusable during normal payments and the subsequent fixes. The original investigation was read-only; implementation status is recorded below and in each resolved issue. Hardware fault-injection testing has not been performed.

Source links refer to the code inspected during the investigation. Function names remain the useful reference if line numbers move. A confirmed defect does not establish that it caused a particular field incident.

## Existing re-presentation recovery

Normal payments save the intended balance, deposit, and counter in `data_to_write`. After failure, [`WRITE_FAILED`](../../main/state_machine.c#L549) retains that target across removal and retries when the same card is presented. It accepts counter/signature mismatches and invalid monetary values, rather than calculating another charge from the damaged payload. There are three immediate attempts per presentation and no overall re-presentation limit in this handler.

With no intervening operation, for a payment targeting counter 101, a reliable reread of physical counter 100 permits the remaining increment of one; counter 101 yields an increment of zero. Both can complete the original payment without charging again. Invalid monetary values can also recover because [`read_card()`](../../main/rfid.c#L167) refreshes the UID and physical counter before returning the range-validation error.

This substantially mitigates issues 02 and 06 while the same transaction remains pending. Recovery still depends on authentication, reliable reads/writes, and a physical counter that has not advanced beyond the target. Cancellation with `D` abandons the retry, and reboot/sleep loses its RAM state. Another terminal does not have the pending record, even if the original terminal still retains it. An actual read failure prevents writing on that presentation, but a later successful presentation can resume. Prefix damage is different: ordinary payments neither modify nor restore its page.

## Priority and implementation order

Priorities are for the reported normal-payment failures: **P0** means fix first because other behavior cannot be trusted; **P1** means high-priority corrections to the existing recovery path; **P2** means condition-specific hardening or recovery beyond the pending transaction. These are implementation priorities, not measured incident frequencies or a ranking of worst-case damage. Issue numbers remain stable; the order below is the recommended work sequence.

| Order | Priority | Issue | Why this order / effect of re-presentation |
| --- | --- | --- | --- |
| 1 | P0, fixed | [01: Signature buffer overflow](01-signature-buffer-overflow.md) | The signature helper now calculates into a full digest buffer and copies only five bytes to its caller, protecting payload generation and retries. |
| 2 | P1 | [03: PN532 operation results](03-pn532-operation-results.md) | Reliable operation outcomes and response validation are prerequisites for trustworthy recovery. Transient failures may otherwise recover, but the driver defects remain. |
| 3 | P1 | [04: Counter reads and retries](04-counter-read-and-retry-errors.md) | Fix ignored/false-zero reads and duplicate increments. Once hardware exceeds the saved target, further re-presentations cannot complete it. Depends on 03. |
| 4 | P1 | [05: Stale transaction retries](05-stale-transaction-retries.md) | Validate the saved transaction against fresh state before mutation, preserving ordinary retry while rejecting changes made elsewhere. Shares the pre-write checks from 04. |
| 5 | P1 | [07: Sleep during writes](07-sleep-during-card-writes.md) | Prevent intentional sleep from interrupting writes or discarding the only pending recovery record. Can proceed alongside 03–05. |
| 6 | P2 | [08: Counter overflow](08-counter-overflow.md) | Guard the 65,535 boundary before mutation; retry cannot repair a wrapped target. The small guard can accompany 04; format migration is separate. |
| 7 | P2 | [02: Interrupted multipage writes](02-interrupted-multipage-writes.md) | Ordinary interruptions should recover by re-presentation. Extend recovery across cancellation/reboot or terminals after fixing the defects that undermine the current path. |
| 8 | P2 | [06: Repair rejects damaged payloads](06-repair-rejects-damaged-payloads.md) | Add trusted privileged recovery when pending transaction context is unavailable. Invalid monetary values alone do not block the existing same-terminal retry. Coordinate with 02. |
| 9 | P2, conditional | [09: Permanent authentication lockout](09-permanent-authentication-lockout.md) | Re-presentation cannot unlock a locked card and can repeat incorrect credentials. Promote to P1 if actual authentication failures are observed; ordinary payload inconsistency does not establish this cause. |

For firmware without the issue 01 fix, the first suspects remain 01, 03, and 04. After deploying that fix, prioritize 03 and 04 for immediate re-presentation failures, then establish whether another operation changed the card as in 05. Simple interruption alone is not sufficient to explain persistent failure under otherwise healthy recovery conditions.

Validate the existing same-terminal recovery path before treating a journal or card-format redesign as necessary for it. Durable transaction records or a new format address additional guarantees after pending state is lost or unavailable; they are not prerequisites for the recovery the current code already attempts.
