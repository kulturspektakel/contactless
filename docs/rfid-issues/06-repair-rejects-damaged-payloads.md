# Fresh repair cannot recover arbitrary damaged payloads

Confidence: confirmed workflow limits. Impact: some damaged cards are unusable through normal terminal workflows despite potentially writable storage. Issue 05 deliberately rejects unknown damage even with a pending transaction; recognized interruptions still recover.

## Defect

[`privileged_repair()`](../../main/state_machine.c#L821) accepts only `CARD_DETECTED_OK` or `CARD_DETECTED_SKIPPED_SECURITY`; invalid cards go to the error screen. [`initialize_card()`](../../main/state_machine.c#L847) also excludes invalid cards. However, this does not describe the normal payment retry: [`write_failed()`](../../main/state_machine.c#L549) explicitly accepts `CARD_DETECTED_INVALID` for the saved UID and resumes the retained intended transaction.

For out-of-range balance/deposit data, [`read_card()`](../../main/rfid.c#L110) obtains the physical counter and updates `current_card` before reporting invalid values. Historically, a pending retry could overwrite those bytes using the saved target. After [issue 05](05-stale-transaction-retries.md), accepting the event only starts preflight: the fresh raw image must match the authorized original, exact intended image, or an allowed ordered whole-page prefix at the original physical counter. At the target counter, only the exact intended image is accepted, without mutation.

The fresh repair operation instead takes surviving values and signs them with an incremented physical counter. It lacks the original payment's trusted intended values. Range validation alone cannot establish that plausible but unauthenticated monetary data is correct.

## Failure scenario

An interruption between completed page writes can recover on the same pending terminal without calculating another charge. There are three attempts per presentation and no overall retry limit. An unknown intra-page tear producing out-of-range monetary bytes now fails closed even there, as do out-of-order mixtures and unrelated signed states. The existing remove/retry UI remains pending; it does not cancel or reconcile automatically. See [interrupted writes and pending recovery](02-interrupted-multipage-writes.md).

If the operator cancels, the terminal reboots/sleeps, or the card goes elsewhere, the pending workflow is unavailable. Fresh repair and initialization reject the invalid values. This is software-unrecoverability through those workflows, not proof of physical damage or irreversible locking.

A damaged prefix is different. [`read_card()`](../../main/rfid.c#L151) exits before refreshing `current_card`; an immediate retry might still match stale identity, but the [writer](../../main/rfid.c#L394) checks fresh raw bytes before touching pages 11–14. It rejects changed untouched bytes and cannot repair prefix page 8. Prefix damage can arise during enrollment or other corruption; normal payment writing does not touch that page. Monetary bytes all share page 12, so intact-page interruptions retain complete old or new values.

## Proposed fix

The [buffer](01-signature-buffer-overflow.md), [transport](03-pn532-operation-results.md), [counter](04-counter-read-and-retry-errors.md), and [stale-retry](05-stale-transaction-retries.md) fixes are implemented. Preserve their guarded recovery and address [sleep protection](07-sleep-during-card-writes.md) next. The exact originally observed bad-signature or mismatched-counter image remains accepted for explicitly authorized repair: it is retained before normalization, not reconstructed from decoded fields. A durable journal is not required for recognized RAM-backed recovery.

For unknown damage or recovery beyond that context, add an explicitly privileged workflow that inspects raw state and hardware identity even when parsing fails, preserving authentication and access controls. Obtain intended monetary values from a trusted durable transaction record or explicit reconciliation. Such records must precede card mutation; success-only logging cannot reconstruct every interrupted transaction.

Restore only the necessary application pages and reconcile the physical counter against that evidence. Never bypass validation merely to re-sign arbitrary unauthenticated balances. Without sufficient evidence, require operator reconciliation or replacement. Distinguish unreadable memory, authentication failure, and hardware locking from malformed payloads; these have different recovery constraints.

## Verification

Verify positive ordered-page recovery, complete-target no-rewrite paths, and original damaged repair baselines. Require refusal of unknown range-invalid tears, unrelated signed states, changed prefixes, and failed reads. Contrast fresh repair after cancellation/reboot with retained snapshots. Host suites pass 70 RFID and 78 PN532 cases under ASan/UBSan, but do not integrate the state-machine event consumer or logging. Validate workflow/accounting behavior on hardware. Any new privileged recovery must require trusted evidence and full readback; ordinary payments must keep rejecting damaged cards.
