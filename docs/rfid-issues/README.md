# RFID write and recovery issues

These documents record the investigation into cards becoming unusable during normal payments and the subsequent fixes. The original investigation was read-only; implementation status is recorded below and in each resolved issue. Hardware fault-injection testing has not been performed.

Source links refer to the code inspected during the investigation. Function names remain the useful reference if line numbers move. A confirmed defect does not establish that it caused a particular field incident.

## Existing re-presentation recovery

Normal payments save the intended balance, deposit, and counter in `data_to_write`, plus the originally observed raw payload in `data_before_write`. After failure, [`WRITE_FAILED`](../../main/state_machine.c#L549) retains that transaction across removal and retries when the same card is presented. The handler accepts counter/signature mismatch and invalid-value events, but the writer now checks the fresh bytes against the saved transaction before modifying anything. It never calculates another charge from damaged data. There are three immediate attempts per presentation and no overall re-presentation limit in this handler.

For a payment targeting counter 101, physical counter 100 permits only the original payload, the intended payload, or an ordered prefix of intended whole pages followed by original pages. A complete intended payload needs only its remaining increment. Physical counter 101 requires the exact intended payload and causes no page rewrites or increment. A different signed state, out-of-order mixture, or unknown intra-page damage requires explicit reconciliation, even while the transaction remains pending. The original observed bad-signature or mismatched-counter payload remains a valid starting image for an explicitly authorized repair.

This mitigates ordinary interruptions between page writes while the same transaction remains pending. Unknown tears are not automatically repaired. A rejected state remains on the existing remove/retry screen; it is not automatically cancelled or reconciled. Cancellation with `D` abandons the retry, and reboot/sleep loses its RAM state. Another terminal does not have this pending record. Failed reads block mutation; a later valid presentation can resume. Ordinary payments neither modify nor restore prefix page 8.

The raw comparison cannot identify which terminal produced a byte-identical target without a transaction identifier or shared record. Reads and writes are also not atomic against another reader. Current host validation passes 70 RFID and 78 PN532 cases under ASan/UBSan; the state-machine event consumer and logging are not integrated into these suites, so accounting deduplication still needs integration testing.

## Priority and implementation order

Priorities are for the reported normal-payment failures: **P0** means fix first because other behavior cannot be trusted; **P1** means high-priority corrections to the existing recovery path; **P2** means condition-specific hardening or recovery beyond the pending transaction. These are implementation priorities, not measured incident frequencies or a ranking of worst-case damage. Issue numbers remain stable; the order below is the recommended work sequence.

| Order | Priority | Issue | Why this order / effect of re-presentation |
| --- | --- | --- | --- |
| 1 | P0, fixed | [01: Signature buffer overflow](01-signature-buffer-overflow.md) | The signature helper now calculates into a full digest buffer and copies only five bytes to its caller, protecting payload generation and retries. |
| 2 | P1, implemented | [03: PN532 operation results](03-pn532-operation-results.md) | Complete validated exchanges, failure resynchronization and counter read-back are implemented. Host fault-injection tests pass; hardware validation is pending. |
| 3 | P1, implemented | [04: Counter reads and retries](04-counter-read-and-retry-errors.md) | Each attempt checks a fresh UID/counter against the saved baseline and target before page writes. Increments require the expected physical counter; already-incremented retries do not increment again. Hardware validation is pending. |
| 4 | P1, implemented | [05: Stale transaction retries](05-stale-transaction-retries.md) | Fresh raw payloads must match the original image, intended image, or an allowed ordered page prefix. Unrelated same-counter contents are rejected before mutation; complete targets are not rewritten. Hardware validation is pending. |
| 5 | P1, next | [07: Sleep during writes](07-sleep-during-card-writes.md) | Prevent intentional sleep from interrupting writes or discarding the only pending recovery record. |
| 6 | P2, partially addressed | [08: Counter overflow](08-counter-overflow.md) | Issue 04 rejects wrapped targets before mutation and rejects unrepresentable physical counters. Explicit exhaustion handling/replacement remains; format migration is separate. |
| 7 | P2 | [02: Interrupted multipage writes](02-interrupted-multipage-writes.md) | Ordered interruptions between completed page writes can recover by re-presentation. Extend recovery across unknown tears, cancellation/reboot, or terminals using trusted evidence. |
| 8 | P2 | [06: Repair rejects damaged payloads](06-repair-rejects-damaged-payloads.md) | Add trusted privileged recovery for unknown damage or unavailable pending context. Issue 05 deliberately rejects unexplained bytes even on the original terminal. Coordinate with 02. |
| 9 | P2, conditional | [09: Permanent authentication lockout](09-permanent-authentication-lockout.md) | Re-presentation cannot unlock a locked card and can repeat incorrect credentials. Promote to P1 if actual authentication failures are observed; ordinary payload inconsistency does not establish this cause. |

For firmware without the fixes, the first suspects remain 01, 03, and 04. Issues 01, 03, 04, and 05 are implemented; issue 07's sleep protection is next. Interruption between intact page writes alone should not cause persistent failure under otherwise healthy pending recovery, but unknown intra-page damage now deliberately requires reconciliation.

Validate the existing same-terminal recovery path before treating a journal or card-format redesign as necessary for it. Durable transaction records or a new format address additional guarantees after pending state is lost or unavailable; they are not prerequisites for the recovery the current code already attempts.
