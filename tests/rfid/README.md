# RFID transaction regression tests

Run from the repository root with Clang available:

```sh
sh tests/rfid/run.sh
```

The harness includes the actual `main/rfid.c`, its payload builders, card reader,
writer, and three-attempt task loop. It uses the real application state and
nanopb structures. A stateful mock tag supplies UID, pages, authentication, and
counter operations; fault hooks model partial page writes, failed fresh reads,
lost increment responses, changed cards, and failed verification reads. The
production retry algorithm is not reproduced in test code.

Cases check rejection before page writes for unexplained counters and invalid
transactions, fresh counter reads on each attempt, fixed baseline/target
snapshots, bounded card reselection, exact-once increment recovery, final target
verification, and same-target re-presentation. Boundary cases cover counters
65,534 and 65,535 and rejection of a wrapped target. The companion
[`tests/pn532`](../pn532/README.md) suite checks actual wire framing, full 24-bit
counter handling, and the driver's expected-counter guard.

Payload reconciliation cases cover all ordered page-write interruption
boundaries, every out-of-order baseline/target page mixture, failed fresh payload
reads, and valid unrelated monetary contents at either allowed counter. A fully
written target must not be rewritten: it needs one increment at the baseline
counter and no mutation at the target counter. The baseline is captured through
the production reader, including invalid raw characters before normalization;
repair tests retain that original damaged image across retries. A deliberate
test-hash collision exercises rejection of a self-consistently signed mixture
even when it resembles an interrupted write.

The host build uses AddressSanitizer and UndefinedBehaviorSanitizer, including
alignment checking. Its temporary executable is removed afterward. A
deterministic hash replaces SHA-1; a standard Base64 implementation replaces
the SDK function while retaining its terminating-NUL behavior. These seams test
payload plumbing and retry behavior, not cryptographic compatibility.

Initialization and crew smoke cases verify separation from normal-payment
counter guards. Initialization fault cases stop at authentication or the first
page write, before the separately documented initialization buffer overrun;
they do not establish successful enrollment or password/configuration safety.
The state-machine event consumer is not executed: the harness supplies an
already-authorized pending transaction and exits at the writer's terminal
event. Logging deduplication, RF timing, physical EEPROM behavior, and real task
concurrency still require hardware/integration testing.
