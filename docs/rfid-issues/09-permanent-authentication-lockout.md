# Repeated incorrect passwords can permanently lock protected writes

Status: confirmed configuration and hardware risk for the assumed Ultralight EV1 model. It is not a demonstrated explanation for the reported normal-payment incidents.

## Defect

[initialize_card()](../../main/rfid.c#L398) sets `AUTH0 = 4` and `AUTHLIM = 7`. Ultralight EV1 permanently locks protected access when its configured failed-password limit is exhausted; subsequent correct passwords cannot unlock it. Successful authentication before exhaustion resets the failure counter. See the [NXP datasheet, section 8.6.2](https://www.nxp.com/docs/en/data-sheet/MF0ULX1.pdf).

The [RFID retry loop](../../main/rfid.c#L521) makes up to three write attempts per presentation. It does not distinguish a definitive incorrect-password response from an uncertain transport outcome. Further presentations can therefore repeat a credential failure until the limit is exhausted; the three-attempt loop is not an overall credential-attempt limit.

Normal payment writes do not modify password or access-configuration pages. A counter/signature mismatch alone does not consume password attempts. The incorrect-password condition must be established separately; ordinary RF/I2C failures must not automatically be classified as incorrect passwords.

## Failure scenario

A card's stored password differs from the UID-derived password the terminal supplies. Each attempted write fails authentication. Retrying the same credentials repeatedly can turn an initially recoverable credential/provisioning problem into permanent write lockout.

One possible origin is interrupted provisioning: the initializer writes protection configuration before PACK and writes PWD last. If interrupted, a card may contain an application payload but retain its default or an incomplete password. This is an initialization risk, not evidence that a normal payment rewrites PWD. Exact activation timing must be confirmed for the deployed tag; the code inspection does not prove that every uninterrupted initialization fails.

With the configured write-only protection, permanent lockout can leave a card readable but unusable for further payments. It is not necessarily invisible to the reader.

## Effect of re-presentation recovery

Re-presentation helps when communication recovers and the correct credentials still work. Retaining the original balance and counter cannot overcome an incorrect password or permanent lockout. The [normal `WRITE_FAILED` handler](../../main/state_machine.c#L549) permits further same-card retries without an overall attempt limit, so the retry policy must treat confirmed credential rejection differently from ordinary payload inconsistency. Investigate this condition when authentication actually fails; normal payment writes do not change the stored password.

## Proposed fix

Introduce structured driver outcomes so a definitive password rejection stops automatic credential retries and triggers diagnosis. Treat ambiguous transport failures as uncertain; do not blindly cycle through passwords or assume the card's failure budget is intact. This depends on [reliable PN532 response handling](03-pn532-operation-results.md).

Review whether permanent hardware lockout is a desired policy. If it is not, `AUTHLIM = 0` disables this limit, at the cost of removing that password-guessing defense. Treat this as an explicit security/product decision, not an unconditional reliability fix. Existing accessible cards require authenticated, verified configuration migration; already locked cards cannot be unlocked by changing firmware.

For provisioning, identify the actual card model and configure PWD/PACK before enabling protection and a retry limit. Verify credentials through authentication and PACK comparison, then verify the intended configuration and behavior after re-selection or an RF reset. Payload readback alone is insufficient. Preserve a recoverable provisioning state and avoid treating a nonempty payload as proof of completed security setup.

## Verification

On expendable test cards of the deployed model, establish the failure-limit semantics, persistence across field resets, and successful-authentication reset behavior. Exercise wrong credentials and transport failures separately. Interrupt each provisioning stage and check recovery. Confirm that normal payment error handling cannot perform uncontrolled credential retries and that the final UI distinguishes unreadable data, inconsistent payload, authentication failure, and confirmed permanent lockout.
