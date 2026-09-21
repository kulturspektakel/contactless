# Signature generation overruns the payload buffer

## Defect

Confidence: confirmed from the application and the linked ESP-IDF SHA-1 implementation. Impact: undefined behavior during regular-card writes and regular/crew initialization; possible crashes or corruption of transaction data. The actual consequence depends on stack layout and cannot be established from source alone.

The [payload field lengths](../../main/rfid.h#L3) total 17 bytes: UID 7, counter/expiry 2, deposit 1, balance 2, and signature 5. Both [regular_card_payload](../../main/rfid.c#L245) and [crew_card_payload](../../main/rfid.c#L255) allocate exactly 17 bytes and pass `buffer + 12` to `calculate_signature_ultralight`. Only five writable bytes remain.

[calculate_signature_ultralight](../../main/rfid.c#L57) calls [create_sha1_hash](../../main/http_auth_headers.c#L10), which passes that pointer directly to `mbedtls_sha1_finish`. The linked ESP-IDF implementation copies the full 20-byte digest to its output. It therefore writes 15 bytes beyond the payload allocation. Keeping only five signature bytes in the card format does not make the SHA-1 function produce a five-byte digest.

## Failure scenario

A payment reaches payload generation after authentication. Producing its signature overwrites adjacent stack memory before the first payload page is written. Depending on the overwritten data, execution may fail immediately, continue with damaged state, or appear successful. A resulting failure during the subsequent sequence of page writes can leave the card with mismatched data, signature, or counter.

The same memory defect exists when initializing or renewing a crew card. Existing retries and card readback do not prevent out-of-bounds memory writes; retries execute the same faulty calculation again. This is a definite defect, but attributing a particular damaged card to it requires runtime evidence.

## Effect of re-presentation recovery

The [same-card retry](../../main/state_machine.c#L549) retains the intended transaction, avoiding another charge calculation, but it still invokes this payload builder on every write attempt. Removal and re-presentation do not eliminate the overflow. A retry may happen to succeed despite the defect; that does not make the memory access safe. This remains the first implementation priority.

## Proposed fix

In each payload builder, calculate the signature into a correctly sized 20-byte temporary buffer, then copy only `LENGTH_SIGNATURE` bytes into the payload at `OFFSET_SIGNATURE`. Preserve the existing hash input, byte order, first-five-byte truncation, and card format.

Keep the general SHA-1 helper's full-digest contract. [Password and PACK derivation](../../main/rfid.c#L217) needs digest bytes 14–19, so globally truncating that helper would break authentication. Consider making output-size expectations explicit in the signature helper's interface.

## Verification

Use known regular and crew fixtures to verify byte-for-byte identical encoded payloads and signatures. Run the actual payload builders with memory instrumentation or guarded buffers and verify no writes outside their allocations. Verify password and PACK derivation remains identical for existing UID/salt fixtures. Include both callers; exercising only regular transactions misses the crew path.
