# Desktop asynchronous encrypted messaging (3C.2C)

This headless runtime connects the portable Noise K mailbox coordinator to the
existing nonblocking TCP adapter and Python relay. It is independent of LVGL and
the legacy `falafel_app` event queue: layering that queue on top would duplicate
its retry/ACK/deduplication rules. Application effects use the mailbox's explicit
peek/effect/consume contract.

Build with the root CMake project, then run from three terminals:

```sh
mkdir -p /tmp/falafel-encrypted-demo
./build/tests/mailbox_peer provision /tmp/falafel-encrypted-demo
python3 relay/falafel_relay.py --e2ee-only --pair 1:2 --port 7777 \
  --data /tmp/falafel-encrypted-demo/relay.json
./build/tests/mailbox_peer run /tmp/falafel-encrypted-demo 1 7777 10000 consume 1 'hello'
./build/tests/mailbox_peer run /tmp/falafel-encrypted-demo 2 7777 10000 consume 0
```

`provision` is **test/demo provisioning**, generating a trusted pair directly,
not a replacement for SAS pairing. It creates 0600 local `.identity` files
(private key + paired public key) and empty mailboxes. It refuses to overwrite
existing identity files. These credential files are never passed to the relay.
Production callers must load the identity from the established pairing store
and pass it to `falafel_mail_open`; production provisioning/Keychain wiring for
this executable is not supplied. Use a trusted local directory. Do not run this
fixture provisioner as an Internet-facing service.

`run ROOT OWN PORT DURATION_MS MODE COUNT [TEXT]` reloads an existing mailbox,
enqueues COUNT new random IDs (0..9), then polls TCP and mailbox state. COUNT=0
only retries existing outbox records. OWN is 1 or 2. `consume` stores an idempotent
application effect then calls consume; `hold` leaves inbox available; `crash`
exits with status 23 after effect commit before consume (test mode). Duration is
finite so a later run can demonstrate offline recovery. Default text is a test
marker, maximum text 117 bytes. Summaries contain states/counters, not content.
`FALAFEL_TEST_IO_CHUNK=3` forces small socket calls for reproducible partial I/O.

The minimal application effect is a durable index of `(sender public key,
message ID, SHA256(plaintext))`, one atomic file per ID. Repeating an effect
verifies the same record instead of appending it. Plaintext is transient and
securely erased. An existing mismatched/corrupt effect stops consumption.
A crash after effect before consume reoffers the ID; restart reuses the same
index entry and then consumes. This is not a generic exactly-once application
transaction. Other consumers must implement the same idempotence contract.

This unupgraded headless run mode retains eight lifetime records per direction, including completed or
consumed records. FULL is explicit, preserves existing records and never causes
auto-eviction. Relay capacity must accommodate both data and saved receipts
(default 512, at most 32 cache entries for this fixed paired mailbox workload).
There is no automatic relay cache removal either. See
`docs/async-e2ee-design.md` and `docs/protocol.md` for wire format and limitations.

Tests use separate A, B and relay subprocesses, including online/offline handoff,
sender shutdown, relay restart, saved receipt recovery with both peers offline
at different times, three-byte socket calls, a fragmenting/disconnecting proxy,
forged frames, backpressure, full inbox/outbox and consumer restart:

The fragmented-disconnect test snapshots persisted outbox ciphertext before
networking. `envelopes()` only reads mailbox bytes; it never encrypts. Relay
arrival order may differ from mailbox slot order after a round-robin retry, so
the test matches IDs and multiplicity while requiring exact full-frame bytes.
Proxy captures also check every complete attempt and interrupted frame prefix
against the snapshot; mailbox bytes must remain unchanged through ACK replay.

```sh
ctest --test-dir build/tests -R encrypted_tcp_end_to_end --output-on-failure
```

Run the full root suite in Debug, Release and ASan/UBSan as described in the main
README. Local TCP socket permission is required. This is a desktop prototype;
the C6 lab now integrates Wi-Fi, TCP, the same mailbox/link and NVS with explicit
temporary credentials (cross-built and host-tested, not physically verified).
See `docs/c6-relay-transport.md`; no headless wire format changes. The LVGL
simulator now reuses this runtime architecture in a separate worker, with a
encrypted persistent history, consumption after durable idempotent effects and
archive-backed active-slot reuse. Its local FMBX v2 snapshots require the matching
archive and cannot be used by this run mode; use fresh legacy fixture stores for
these examples. The simulator explicitly migrates v1 stores with --init-history. See
`simulator/README.md` for two-window commands and tests. TCP registration and routing metadata remain unauthenticated; no TLS or
availability against a hostile relay is claimed.
