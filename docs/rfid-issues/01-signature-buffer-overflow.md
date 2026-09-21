# Signature generation overruns the payload buffer

Status: fixed in `calculate_signature_ultralight()`. The helper now writes exactly `LENGTH_SIGNATURE` bytes to its caller's output. The analysis below describes the original defect.

## Original defect

Confidence: confirmed from the application and the linked ESP-IDF SHA-1 implementation. Impact: undefined behavior during regular-card writes and regular/crew initialization; possible crashes or corruption of transaction data. The actual consequence depends on stack layout and cannot be established from source alone.

The [payload field lengths](../../main/rfid.h#L3) total 17 bytes: UID 7, counter/expiry 2, deposit 1, balance 2, and signature 5. Both [regular_card_payload](../../main/rfid.c#L250) and [crew_card_payload](../../main/rfid.c#L260) allocate exactly 17 bytes and pass `buffer + 12` to `calculate_signature_ultralight`. Only five writable bytes remain.

Before the fix, [calculate_signature_ultralight](../../main/rfid.c#L57) passed its output directly to [create_sha1_hash](../../main/http_auth_headers.c#L10), which passes that pointer to `mbedtls_sha1_finish`. The linked ESP-IDF implementation copies the full 20-byte digest to its output. This wrote 15 bytes beyond the payload allocation. Keeping only five signature bytes in the card format does not make the SHA-1 function produce a five-byte digest.

## Failure scenario

A payment reaches payload generation after authentication. Producing its signature overwrites adjacent stack memory before the first payload page is written. Depending on the overwritten data, execution may fail immediately, continue with damaged state, or appear successful. A resulting failure during the subsequent sequence of page writes can leave the card with mismatched data, signature, or counter.

The same memory defect also affected initialization and crew-card renewal. Existing retries and card readback did not prevent the out-of-bounds writes; retries executed the faulty calculation again. This was a definite defect, but attributing a particular damaged card to it requires runtime evidence.

## Effect of re-presentation recovery

The [same-card retry](../../main/state_machine.c#L549) retains the intended transaction, avoiding another charge calculation, but invokes the payload builder on every write attempt. Before this fix, removal and re-presentation repeated the overflow. Centralizing the fix in the signature helper protects every caller, including all retry attempts.

## Implemented fix

Calculate the complete digest into a 20-byte temporary buffer inside `calculate_signature_ultralight()`, then copy only `LENGTH_SIGNATURE` bytes to `target`. Its output parameter explicitly names the five-byte signature size, and the verification caller uses a buffer of that size. Both payload builders can continue passing their five-byte signature field directly to the helper.

The hash input, byte order, first-five-byte truncation, and card format are preserved. The general SHA-1 helper retains its full-digest contract: [password and PACK derivation](../../main/rfid.c#L222) needs digest bytes 14–19 and remains unchanged.

## Verification

The updated `rfid.c` compiled successfully with the recorded ESP32-S3 compile command. The compiler reported only the existing missing-semicolon warning in `state_machine.h`.

A temporary host harness extracted the actual signature helper, payload builders, and password derivation, using the local ESP-IDF portable SHA-1/base64 implementations. AddressSanitizer and UndefinedBehaviorSanitizer passed for four independently generated regular/crew fixtures, including counter/expiry boundaries. The checks cover exact raw payloads, encoded bytes and terminator, signatures, direct helper calls with a five-byte output buffer, and unchanged password/PACK derivation. A SHA-1 known-answer check also passed.

The same harness reproduced the original stack-buffer overflow under AddressSanitizer separately for regular and crew payload generation using the pre-fix source from commit `f62bab0`.

These checks do not exercise the hardware SHA accelerator, RF writes, or the full firmware. The independent initialization base64-buffer sizing defect is outside issue 01; this verification uses correctly sized encoded-output buffers and does not claim all initialization memory defects are resolved.
