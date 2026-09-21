# Automatic sleep can interrupt an active card transaction

Confidence: confirmed missing coordination; corruption depends on shutdown timing. Impact: sleep can leave an incomplete payload, mismatched counter, or unfinished enrollment requiring recovery.

## Defect

The [power-off callback](../../main/power_management.c#L162) skips shutdown for USB power and battery-test mode, but not for `WRITE_CARD` or `WRITE_CARD_INITIALIZE`. It sends `ENTER_POWER_SAVE` and proceeds directly to configuring wakeup pins and calling `esp_deep_sleep_start()` without waiting for RFID completion.

The state machine [accepts `ENTER_POWER_SAVE` globally](../../main/state_machine.c#L1016), including during a write. Its [timer reset logic](../../main/state_machine.c#L1023) covers keypad events, not card activity. The timeout is [15 minutes](../../main/power_management.c#L31), and is started on a detected USB disconnection. Repeated card activity therefore does not necessarily postpone it.

The callback also drives GPIO14, named `RSTPDN`, low. However, [PN532 initialization uses GPIO48 as reset](../../main/rfid.c#L444). The actual effect of GPIO14 requires hardware confirmation; the defect does not depend on asserting that it resets the PN532. Deep sleep alone stops the firmware from completing remaining transaction commands.

## Failure scenario

An operator repeatedly enrolls cards or performs card-only activity after the shutdown timer has started. Near its deadline, the RFID task writes some payload pages. The timer callback runs before the remaining pages, counter increment, or enrollment configuration writes complete and enters sleep.

On wakeup, any volatile intended transaction state is lost. The card can have the same inconsistent state caused by removal during a write; see [interrupted multipage writes](02-interrupted-multipage-writes.md). A last keypad press usually refreshes the deadline during an ordinary sale, reducing that scenario's frequency, but there is no transaction-level exclusion guaranteeing safety.

## Effect of re-presentation recovery

Removal alone preserves the normal pending transaction, so re-presentation can rewrite its intended values. Sleep loses that RAM context. It can also occur while the terminal is already waiting in `WRITE_FAILED`, after card I/O has stopped but before the customer re-presents the card. Guarding only active I/O would leave that second loss-of-recovery window open.

## Proposed fix

Coordinate transaction start and shutdown start through one shared mechanism. Atomically establish either an active write reservation or a shutdown-pending state: shutdown must wait for the active operation to finish, and a new operation must not start after shutdown has claimed the device. Checking the UI mode once is insufficient because it races with a write beginning immediately afterward.

Have the timer callback post a shutdown request to an appropriate task. Do not block the FreeRTOS timer-service task waiting for RFID, or hold a critical section across card I/O. Complete payload verification and transaction bookkeeping before acknowledging safe shutdown. If a failed transaction remains unresolved, defer intentional sleep while its only recovery record is in RAM, or persist a recoverable record before allowing sleep. An idle RFID task alone does not mean the transaction is resolved.

Refresh inactivity tracking on relevant card activity as well as keypad input. Retain durable recovery for unavoidable power loss; coordinating intentional sleep cannot prevent brownouts or physical card removal. Counter outcomes must follow the [counter-read recovery rules](04-counter-read-and-retry-errors.md).

## Verification

Request sleep before a transaction, during each payload/configuration write, during counter increment, during readback, and while awaiting re-presentation in `WRITE_FAILED`. Verify that existing operations finish before sleep and new operations cannot race past shutdown; unresolved recovery records must survive or postpone sleep. Cover repeated card-only activity, USB changes, write failure, and requests arriving simultaneously with transaction start. Confirm the timer-service task remains responsive throughout.
