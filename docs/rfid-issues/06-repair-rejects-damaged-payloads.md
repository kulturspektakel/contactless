# Fresh repair cannot recover every invalid payload after pending retry ends

Confidence: confirmed distinction between pending retry and fresh repair. Impact: some damaged cards become unusable through normal terminal workflows after transaction context is abandoned or lost, despite potentially writable storage.

## Defect

[`privileged_repair()`](../../main/state_machine.c#L821) accepts only `CARD_DETECTED_OK` or `CARD_DETECTED_SKIPPED_SECURITY`; invalid cards go to the error screen. [`initialize_card()`](../../main/state_machine.c#L847) also excludes invalid cards. However, this does not describe the normal payment retry: [`write_failed()`](../../main/state_machine.c#L549) explicitly accepts `CARD_DETECTED_INVALID` for the saved UID and resumes the retained intended transaction.

For out-of-range balance/deposit data, [`read_card()`](../../main/rfid.c#L171) obtains the physical counter and updates `current_card` before reporting invalid values. A pending retry can therefore overwrite damaged monetary bytes with the previously saved valid target. It neither needs privileged repair nor derives the amount from those damaged bytes.

The fresh repair operation instead takes surviving values and signs them with an incremented physical counter. It lacks the original payment's trusted intended values. Range validation alone cannot establish that plausible but unauthenticated monetary data is correct.

## Failure scenario

An interrupted individual-page write or another corruption source leaves out-of-range monetary bytes. Keeping the same terminal in `WRITE_FAILED` and re-presenting the card can restore them, assuming authentication, counter reads, and subsequent writes succeed. Each presentation has three attempts, with no overall retry limit. See [interrupted writes and pending recovery](02-interrupted-multipage-writes.md).

If the operator cancels, the terminal reboots/sleeps, or the card goes elsewhere, the pending workflow is unavailable. Fresh repair and initialization reject the invalid values. This is software-unrecoverability through those workflows, not proof of physical damage or irreversible locking.

A damaged prefix is different. [`read_card()`](../../main/rfid.c#L151) exits before refreshing `current_card`; an immediate retry might still match its stale identity, but [ordinary writing](../../main/rfid.c#L314) only touches pages 11–14 and cannot repair prefix page 8. Prefix damage can arise during enrollment or from other corruption; normal payment writing does not touch that page. Likewise, interruptions between intact payment-page writes normally retain complete old or new monetary values because all monetary bytes share page 12.

## Proposed fix

Preserve the existing pending recovery and first address the [buffer overflow](01-signature-buffer-overflow.md), [transport](03-pn532-operation-results.md), [counter-read](04-counter-read-and-retry-errors.md), and [stale-retry](05-stale-transaction-retries.md) defects. A durable journal is not required to restore the retained intended transaction while its context remains available.

If recovery beyond that context is required, add an explicitly privileged workflow that inspects raw state and hardware identity even when application parsing fails, while preserving authentication and access controls. Obtain intended monetary values from a trusted durable transaction record or explicit reconciliation. Such records must precede card mutation; success-only logging cannot reconstruct every interrupted transaction.

Restore only the necessary application pages and reconcile the physical counter against that evidence. Never bypass validation merely to re-sign arbitrary unauthenticated balances. Without sufficient evidence, require operator reconciliation or replacement. Distinguish unreadable memory, authentication failure, and hardware locking from malformed payloads; these have different recovery constraints.

## Verification

Demonstrate positive same-pending recovery of out-of-range monetary data, signature mismatch, and interrupted writes without charging twice. Contrast fresh repair after cancellation/reboot with the retained-target path. Exercise damaged prefixes, unreadable counters, interrupted enrollment, and unrelated card changes. For any new privileged workflow, verify trusted-record recovery, refusal without evidence, and full readback validation. Ordinary payments must continue rejecting damaged cards.
