# Counter exhaustion corrupts the payload before rejection

## Defect

Confidence: confirmed from integer widths and operation ordering. Impact: a card reaching the application counter limit becomes unusable for ordinary transactions and repair; hardware counter capacity remains available.

The card's physical counter is 24 bits ([NXP datasheet, section 8.7](https://www.nxp.com/docs/en/data-sheet/MF0ULX1.pdf)), but the [application counter](../../main/state_machine.h#L98) and [payload field](../../main/rfid.h#L4) are 16 bits. [mfu_read_counter](../../main/pn532.c#L1302) reconstructs only two response bytes, silently discarding the physical counter's high byte.

The state machine [increments the transaction target](../../main/state_machine.c#L386) into a `uint16_t` without checking exhaustion. [write_card](../../main/rfid.c#L314) writes all four mutable payload pages before checking the counter difference. Its negative-difference guard therefore detects wraparound only after altering the card.

## Failure scenario

A valid regular card has physical and payload counter 65,535. A purchase, top-up, or repair constructs target counter zero through 16-bit conversion of 65,536. The writer stores that zero counter, the requested monetary values, and their signature. It then calculates `0 - 65535`, rejects the negative difference, and leaves the physical counter unchanged.

Subsequent reads find payload counter zero versus physical counter 65,535 and reject normal use. A pending retry retains the same wrapped target; ordinary repair constructs it again. Both repeat the failure. The chip is not inherently destroyed, but the existing transaction and repair paths cannot advance it safely. Since monetary values may already have changed, recovery must reconcile the interrupted transaction too.

If another writer has already advanced a physical counter above 65,535, the current reader hides that condition by returning only the low 16 bits.

## Effect of re-presentation recovery

The same-card retry accepts the counter mismatch and returns to the writer, but preserving target zero cannot reconcile it with physical counter 65,535. Further presentations repeat the negative-difference failure. Unlike an ordinary partial write, there is no valid remaining increment for this saved target. This is a boundary case to guard, not evidence that typical low-counter interruptions are unrecoverable.

## Proposed fix

For the existing format, read and retain the complete 24-bit hardware counter in a wider application type. Reject out-of-format-range values explicitly. Check exhaustion before constructing or narrowing the next target counter, generating its payload, or making any card mutation. Validate the target difference before writing pages, preserving legitimate zero-difference retries after a confirmed increment. Counter equality alone does not authorize a retry: apply the [transaction reconciliation rules](05-stale-transaction-retries.md) as well.

At exhaustion, provide an explicit replacement/reconciliation workflow that preserves the owner's value. Do not reset the card's balance to make it reusable or silently apply modulo arithmetic to its security counter.

A larger on-card counter requires a versioned format migration covering signature inputs, payload layout, backend processing, and all other readers/writers. Widening the C member alone is insufficient.

## Verification

Exercise counters 65,534, 65,535, and 65,536. Verify the last representable transition succeeds, while exhausted/out-of-format cards cause no payload writes or increment commands. Cover payment and repair, plus a retry whose physical counter already equals its valid target. Verify all three hardware counter bytes are preserved and exhaustion leaves monetary values unchanged.
