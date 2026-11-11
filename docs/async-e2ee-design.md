# Milestone 3C.2A: asynchronous authenticated encryption — implemented

The portable C11 module `shared/e2ee/falafel_async_e2ee.c` implements one offline
message as the **handshake payload of Noise_K_25519_AESGCM_SHA256**. It does not
require a live connection, a response, or a previous live session. Both static
X25519 keys must already be bilaterally authenticated by pairing. This is an
isolated cryptographic module, not runtime store-and-forward integration.

## Construction and references

The construction follows the [Noise specification, revision 34](https://noiseprotocol.org/noise.html),
sections 5 (symmetric state), 6 (prologue), 7.4 (one-way K), 7.7 (payload
security), 12 (algorithms), and 14 (security considerations). Pattern:

```text
K:
  -> s
  <- s
  ...
  -> e, es, ss [encrypted application payload]
```

The implementation specializes this one message, uses the existing provider
for every primitive and RNG operation, and retains no Split/transport state.
The live IK module and pairing lifecycle remain unchanged. Reusing Noise-C's
whole handshake engine would also bring its duplicate reference primitives,
heap allocation and separate RNG backend into the portable module. Instead,
this small specialization uses provider SHA-256 (the sole added primitive API),
HKDF-SHA256, X25519, AES-256-GCM, constant-time comparison and secure erasure.
Bidirectional interoperability tests use the existing pinned Noise-C dependency
as an independent implementation. This is evidence of conformance, not a
security proof or an independent cryptographic audit.

Exact processing (all hashes SHA-256):

1. Initialize `h = ck = protocol_name || zero_padding_to_32_bytes`.
2. `MixHash(header)`, then `MixHash(sender_static)`, then
   `MixHash(recipient_static)` for the two pre-messages.
   `MixHash(x)` means `h = SHA256(h || x)`.
3. Generate fresh X25519 ephemeral keypair using the provider CSPRNG and
   `MixHash(ephemeral_public)`.
4. `MixKey(DH(ephemeral_private, recipient_static))` (es), then
   `MixKey(DH(sender_private, recipient_static))` (ss).
   Each MixKey is RFC 5869 HKDF-SHA256 with salt=current ck,
   IKM=32-byte DH result, empty info, output length=64:
   first 32 bytes replace ck, next 32 replace the AES key. This is exactly
   Noise HKDF with two outputs. Invalid/all-zero DH results fail closed.
5. Encrypt the payload using AES-256-GCM with AAD=h, 16-byte tag and nonce
   `000000000000000000000000`. Noise resets its nonce to zero on MixKey;
   this key encrypts exactly one payload. No random nonce field is needed.
   The post-payload transcript hash/Split are unused and not retained.

The recipient computes es with its private key and the envelope ephemeral key,
and ss with its private key and its trusted paired sender public key. The API
checks that the local public key matches its private key and that header
identities exactly match local and paired identities. Persistent-state fields
are consumed directly; no second identity store or pairing protocol is added.
The caller must supply trusted pairing state; generation/CRC do not authenticate
metadata against malicious local replacement.

## Envelope v1 and transport budget

All fields have fixed offsets; length is unsigned, one byte. No C structs are
serialized. Suite 1 fixes the complete Noise construction above.

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 3 | ASCII `FAE` magic |
| 3 | 1 | envelope version = 1 |
| 4 | 1 | suite = 1 |
| 5 | 1 | plaintext length, 0..122 |
| 6 | 32 | sender static public key |
| 38 | 32 | recipient static public key |
| 70 | 16 | opaque application message ID |
| 86 | 32 | fresh ephemeral public key |
| 118 | 0..122 | ciphertext |
| 118 + length | 16 | GCM tag |

The entire 86-byte header is the Noise prologue. Version, suite, both identities,
length and message ID are thus authenticated; the ephemeral key is bound by
MixHash. Metadata and identities remain visible. Envelope length must be exactly
134 + plaintext length: **134..256 bytes; maximum plaintext 122 bytes**.
Unknown versions/suites, wrong destinations, unpaired identities, trailing data,
truncation, invalid keys and invalid tags are rejected. No transport extension
is needed for this limit. Event-v1 still has a 40-byte header and 256-byte
payload (296 total). A future integration needs an explicit event type and
application length handling; an envelope is not automatically a text event.
Relay, protocol framing and LVGL are unchanged.

## API, errors and platform scope

See `shared/include/falafel_async_e2ee.h`. Encryption/decryption are stateless,
caller-buffer APIs using an initialized provider context and the existing
persistent pairing state type. IDs are opaque 16-byte values supplied by the
application (including zero); uniqueness/deduplication is not enforced here.
Buffer overlap is unsupported; callers serialize access to the RNG context.
There is no heap allocation in the module. Local secret structures and temporary
plaintext are securely erased on every exit. Provider errors propagate; failure
sets output length to zero and clears bounded output, and decrypt clears the
returned ID. No unauthenticated plaintext is published. Output buffers and
length pointers must follow the header contract.

The module has no OS, Noise-C or Mbed TLS headers; its only runtime dependency
is the portable provider. This permits a future ESP32-S3/ESP-IDF component using
the existing Mbed TLS provider. No ESP32-S3 cross-build/hardware validation was
performed for this milestone. Existing ESP-IDF 5.5.2 ECC side-channel limitations
still apply; see PROJECT_STATUS.md. Production requires a verified patched
Espressif release and hardware RNG, stack and lifecycle testing.

## Actual security guarantees and residual limitations

- Noise K authenticates the sender to the paired recipient through ss and
  protects ciphertext and bound metadata against third-party modification,
  assuming uncompromised keys, DH hardness and correct primitives/CSPRNG.
- Authentication is symmetric: the recipient can forge such messages. There
  are no signatures, public verifiability or non-repudiation. Recipient static
  compromise also enables sender impersonation to that recipient (KCI).
- Recipient private-key compromise decrypts recorded messages; there is no
  forward secrecy against recipient compromise. Sender-only static compromise
  does not disclose past payloads if ephemeral secrets were erased and the
  recipient key remains secure; it does permit future sender impersonation.
- Fresh ephemeral generation supplies probabilistic per-message key uniqueness.
  No rollback-resistant hardware counter is required under the accepted model.
  RNG failure, cloned/rolled-back RNG state or repeated ephemeral generation
  can repeat the encryption key and nonce: this is outside that guarantee.
- No replay detection: decrypting the same valid envelope twice succeeds.
  Message IDs are bound, not an anti-replay mechanism. No delivery, ordering,
  freshness, authenticated ACK, censorship resistance or durable recovery is
  claimed. Timing, length, identities and IDs are not hidden.
- This application framing and implementation have not had independent security
  review. Standard primitives alone would not justify a custom construction;
  the rationale here is conformance to the specified Noise K pattern, with
  explicit prologue and independently tested interoperability.

## Verification and reproduction

On 2026-10-30, Debug, Release and ASan/UBSan root suites each passed 14/14;
existing pairing/persistence/IK suites each passed 5/5 in those configurations.
Root tests include provider RFC vectors, core/protocol/persistence, relay and
transport regressions. Initial sandbox socket failures passed when rerun with
localhost permission. Sanitizers cover module/provider wrapper and Noise-C
reference test code; upstream Mbed TLS is built with its normal flags.

New tests cover offline round trips at every length 0..122, repeated plaintext
and ID with distinct envelopes, wrong peer/destination, unpaired state,
inconsistent identity, low-order public key, every single-byte alteration,
every truncation, trailing/oversized data, small buffers, invalid context and
NULL arguments. Fault injection fails each provider call in turn (public-key
reconstruction, generation, hash, both DHs, both HKDFs and AEAD), asserting no
output publication. Noise-C interoperability runs both sending directions for
empty, 61-byte and maximum messages. Tests do not demonstrate RNG health or
formal security.

```sh
cmake -S . -B build/m3c2a-debug -DCMAKE_BUILD_TYPE=Debug \
  -DFALAFEL_ASYNC_NOISE_INTEROP=ON
cmake --build build/m3c2a-debug -j4
ctest --test-dir build/m3c2a-debug --output-on-failure
# Repeat in separate directories with -DCMAKE_BUILD_TYPE=Release, and
# -DCMAKE_BUILD_TYPE=Debug -DFALAFEL_ENABLE_SANITIZERS=ON respectively.
```

CMake fetches the pinned dependencies. Local verification reused already fetched
Mbed TLS 3.6.7 and pinned Noise-C cfe2541 sources through CMake cache overrides.
Interop is optional (OFF by default), so the production module needs no Noise-C.

## 3C.2B: persistent mailbox and authenticated receipts — implemented

The isolated `falafel_mailbox` module wraps the 3C.2A primitives. It does not
change the live core, TCP relay, event codec or LVGL. The raw encryption API still
accepts replay; persistent replay detection belongs to this mailbox layer.

### Reused components and separation

The module consumes the existing persistent pairing identity type, provider,
Noise K envelope implementation and its limits. Existing application event
queues have bounded deduplication and numeric relay/device ACK phases, and
cannot provide cryptographic receipts. The desktop application snapshot codec
serializes cleartext application state, whereas pairing persistence couples
88-byte metadata to Keychain. Neither codec is appropriate for ciphertext mail.
The new POSIX backend follows their atomic replacement strategy, adding mandatory
parent-directory fsync, short-write/EINTR handling and exclusive lifetime locking.
It has no Keychain, SQLite or GUI dependency. The portable C11 library has no
POSIX dependency, heap allocation or filesystem headers.

### Wire data and authenticated ACK format

Both kinds use the **unchanged envelope v1**, with different authenticated
plaintext domains:

| Kind | Noise plaintext | Envelope size |
|---|---|---|
| Data | ASCII `FMSG`, byte version 1, then 0..117 application bytes | 139..256 |
| Receipt | ASCII `FACK`, byte version 1, byte status 1, SHA256(original complete data envelope) | 172 |

Status 1 means **durably accepted in recipient inbox**, not read or consumed.
Unknown labels, versions, statuses and lengths are rejected. The receipt's
Noise sender is the original recipient and its Noise recipient the original
sender; its envelope message ID is the original ID. Thus Noise authentication
binds both identities, ID, version and status, and the encrypted digest binds the
exact original ciphertext. SHA-256 here is only an envelope identifier inside
an authenticated payload, not a custom MAC or a new key schedule. ACK encryption
reuses the existing standard Noise K construction and fresh CSPRNG ephemeral
key. No new cryptographic primitive or custom DH construction is introduced.
Application text is always wrapped in `FMSG`, so application-supplied bytes
cannot be interpreted as a receipt. Data/receipt reflection is rejected.

`falafel_mail_ack` accepts only a peer-authenticated receipt with matching ID and
envelope digest. ACKs for independent messages can arrive in any order; an ACK
for an already confirmed record returns DUPLICATE. A different authenticated
ID returns NOT_FOUND, a different digest AUTH. Legacy relay ACKs (including
relay-reported DEVICE_RECEIVED) never update this mailbox: they are not Noise
envelopes and fail verification. This layer has no read-receipt state.

### Persistent schema and capacity

Each mailbox is tied to one immutable local/paired public-key pair. A new store
must be explicitly created; reopening a missing store returns NOT_FOUND. Never
use create as automatic recovery from deletion/corruption. Identity rotation
requires an explicit future migration/export policy; wrong identity fails closed.

Legacy snapshot v1 is **8,376 bytes**, independent of network framing. The
simulator explicitly migrates to archive-bound v2 (8,392 bytes); see
`application-persistence.md`. Unupgraded headless users retain v1 behavior:


| Offset | Bytes | Meaning |
|---|---:|---|
| 0 | 8 | `FMBX`, byte version 1, three zero reserved bytes |
| 8 | 32 | local static public key |
| 40 | 32 | paired static public key |
| 72 | 8 × 517 | outbox records |
| 4208 | 8 × 517 | inbox records |
| 8344 | 32 | SHA256 of preceding 8344 bytes |

Each record is state (1 byte), envelope length (2-byte big-endian), ACK length
(2-byte big-endian), envelope slot (256 bytes), ACK slot (256 bytes). Unused
bytes must be zero. State 0 is vacant, 1 pending/available, 2 received/consumed.
Outbox ACK slots are empty; inbox ACKs are exactly 172 bytes. IDs and destinations
are retained in the saved envelope; no second potentially inconsistent ID copy
is serialized. The decoder validates length, checksum, reserved fields, state,
identity direction, envelope structure, padding and duplicate IDs per direction.
There are no persisted pointers, private keys or plaintext. Inbox plaintext is
decrypted transiently for verification/peek and erased. Checksum detects
accidental corruption; it does not authenticate hostile local storage edits.

**Legacy v1: eight lifetime outbox IDs and eight lifetime inbox IDs** are retained,
including completed/consumed records. No automatic eviction or deletion API
exists. Full returns FULL without accepting another message; duplicates can
still recover their saved receipt at capacity. This intentionally trades
availability for retained replay history. The simulator now supports local retirement into its durable encrypted archive,
without deleting replay IDs or changing network frames. Compaction of old replay
IDs/epochs remains future work: deleting history would weaken deduplication. The API never silently drops
an accepted pending record. Reusing an outbox ID returns CONFLICT; a receiver
rejects a different authenticated envelope under an existing ID as CONFLICT.
IDs must be unique per direction and pairing for the lifetime of the mailbox.

### Transactions, recovery and delivery contract

`enqueue` encrypts once, serializes a candidate snapshot, atomically commits it,
then publishes the pending state. `retry(slot)` copies the exact stored envelope
and never invokes encryption. Outbox plaintext is never persisted. The caller
must not transmit ciphertext from any source other than a successful retry.

`receive` verifies Noise authentication and the data label before lookup or
acceptance. A new record stores the data envelope **and the generated receipt in
one transaction**. Only after commit is the ACK returned. A duplicate must match
the exact original envelope and retrieves the identical saved ACK, even after
consumption/restart. If an ACK is lost, the sender retries saved data; no separate
ACK queue or re-encryption is required. Receipt generation/storage failure does
not publish acceptance or an ACK. Successful authenticated ACK handling commits
outbox state 2 before returning completion.

Application delivery uses `peek` and `consume(id)`. Peek is non-destructive and
returns a stable ID and plaintext; consume atomically marks that inbox record
consumed. Repeated consume is idempotent. Once consumed durably, the record is
never offered by peek again; its deduplication history and receipt remain.

**This is at-least-once availability until consume, not exactly-once external
application effects.** A crash after an external effect but before consume may
reoffer the same ID. The consumer must transact effects with its own durable
(sender identity, ID) ledger or make them idempotent. It must not consume before
successfully committing its effects. No API automatically invokes an application
callback or claims an atomic transaction with the existing application core.
The process test demonstrates recovery with an example durable ID ledger.

The storage interface has load/atomic-replace callbacks. On replace failure the
commit outcome can be uncertain: the handle is poisoned and all operations
require reopening. The caller receives no ACK/completion in this case. Reopening
recovers the old or new complete snapshot. A corrupt/incomplete target fails
closed, never resets to an empty store. Orphan temporary files are ignored.

Desktop backend: exclusive nonblocking `.lock` file, 0600 same-directory unique
temporary, full write loops, file fsync, close, atomic rename, mandatory parent
fsync. A successful return means the replacement was durably committed under
the filesystem's fsync/rename guarantees. Load also fsyncs a surviving snapshot
and directory before making it usable after an uncertain prior replacement.
The containing directory must already exist and be trusted; callers must not
unlink the lock file. Operations are single-writer and serialized. The caller
owns mailbox memory (roughly 17 KiB including snapshot workspace); embedded
callers should avoid allocating it on a small task stack. The ESP-IDF NVS backend
now implements the callbacks with explicit initialization and poisoned-error
recovery. C6 cross-build and host model tests pass; actual multi-page/GC power-cut
recovery remains unverified. See [embedded storage](embedded-storage.md).

| Crash point | Recovery |
|---|---|
| Before outbox commit | Message not accepted; no retry record exposed |
| After outbox commit, before send | Pending exact envelope survives |
| After send, before receipt | Retry exact bytes; receiver deduplicates |
| During inbox commit | Old or new complete state; no ACK before durable success |
| After inbox commit, before ACK | Sender retry retrieves saved ACK |
| After external application effect | Same ID reoffered until consume; consumer ledger required |
| During ACK/consume update | Old or new state; retry update idempotently after reopen |
| Incomplete/corrupt target | Explicit error; no empty-store fallback |

### Verification of 3C.2B

On 2026-10-30, compiled Debug, Release and ASan/UBSan builds; root suites passed
**15/15 each**, including Noise-C interoperability and relay/transport regression.
Existing pairing/persistence/IK suites passed **5/5 each** in all three builds.
After the last test additions the mailbox test was repeated in all three builds.
`git diff --check` passed. Test commands are the 3C.2A root commands above;
`ctest --test-dir <build> -R mailbox --output-on-failure` selects the new suite.

`test/test_mailbox.c` covers outbox restart/retry identity, empty/max application
messages, persistent deduplication before/after consume, genuine/forged/altered/
wrong-ID/wrong-hash/wrong-status/reflected/unpaired receipts, duplicate and reverse
order ACKs, full stores, exclusive locking, unavailable backend, save failures,
poison/reopen recovery, no ACK on failed inbox save, corrupt/truncated targets,
missing-store policy and absence of plaintext/private keys in a populated file.
A fork/_exit matrix kills a writer at six backend stages plus after commit for
each of enqueue, receive, ACK update and consume (**28 process-crash scenarios**).
It reopens state in a surviving process and verifies old/new transaction state.
A further child dies after application ledger commit before consume, and recovery
uses that ledger to avoid repeating the external effect. These are real process
termination tests, not power-cut/hardware durability tests.

### Residual mailbox limitations

The underlying Noise K security limits remain: no recipient-compromise forward
secrecy, symmetric forgery/KCI, visible metadata and probabilistic uniqueness
under a reliable CSPRNG. Snapshots are checksummed, not protected against
malicious changes, deletion or rollback. Storage rollback can forget consumption
and allow replay; no monotonic hardware witness is claimed. Durable acceptance
assumes functioning storage and filesystem semantics. There is no automatic
repair of a corrupt store, and capacity cannot currently be reclaimed safely.

## 3C.2C: desktop TCP integration — implemented

The new portable `falafel_mail_link` frame codec/coordinator connects the 3C.2B
mailbox to the existing desktop TCP adapter and Python relay. See `protocol.md`
for the explicit v2 wire format and
`experiments/desktop-mailbox/README.md` for commands and fixture provisioning.
The headless encrypted runtime is `mailbox_peer`, with a minimal application
consumer. The LVGL simulator now reuses the same mailbox/link through a dedicated
worker; its encrypted application history now commits before consumption and
retains replay IDs/receipts before active-slot release.
See the integration increment below and `simulator/README.md`.

```text
application text -> mailbox enqueue -> durable outbox -> link v2 DATA
  -> existing nonblocking TCP -> relay durable ciphertext cache -> peer TCP
  -> link -> authenticated mailbox receive -> durable inbox + saved ACK
  -> idempotent effect index -> persistent consume

saved ACK -> link v2 RECEIPT -> TCP -> relay durable cache -> sender TCP
  -> mailbox authenticated ACK verification -> durable outbox completion
```

No pairing or primitive changes were needed. The link does not use the legacy
`falafel_app` queue, numeric event ACKs or its bounded deduplication mechanism;
those would duplicate the mailbox lifecycle. It uses existing `retry`, `receive`,
`ack`, `peek` and `consume`, and existing inbox ACK records. Fair bounded scheduling
has only volatile timer/receipt-pending bits, not another durable queue. One
pending frame at a time goes through the existing transport output buffer; busy
returns retain mailbox state. Reconnect replays pending data and saved receipts.
The relay uses its existing atomic persistence, partial-I/O loops and bounded
buffers, and adds a v2 opaque-frame path without changing v1 semantics.

The minimal consumer's durable effect is a sender/ID/plaintext-hash index, stored
atomically using the POSIX storage backend. It verifies an existing matching
entry before calling consume. A crash between effect and consume reoffers the
same ID; the effect is idempotent. This does not imply exactly-once arbitrary
external effects. The executable's `crash` mode exercises that window; a corrupt
or mismatching effect file stops the consumer.

### Capacity and availability

For the unupgraded headless runtime, mailbox limits remain eight retained IDs
per direction including completed and consumed records. Upgraded simulators
reuse eight active slots per direction after durable archival; their current
application/archive limit is 1024 total IDs/device. At full capacity enqueue explicitly fails; a full inbox does
not generate a receipt, so the sender remains pending. Duplicate envelopes can
still recover their saved receipt. The relay retains both data and receipts in
its bounded cache and never substitutes its own status for a peer ACK. Its full
cache applies backpressure; configuration must reserve enough room for receipts.
No accepted pending message is automatically evicted.

Safe future compaction requires an authenticated epoch/watermark agreed by both
peers, durable state transitions for retries and receipts, a replay policy for
old epochs, and an explicit rollback model. Simply deleting consumed records or
relay frames is not implemented and would undermine the current guarantees.

Relay routing/registration remain unauthenticated and traffic analysis is still
possible. Malicious clients/relay can deny service, fill the cache or poison a
routing ID with conflicting ciphertext; no liveness against active attackers is
claimed. Endpoints fail authentication without changing completion state. The
relay has no private keys or decryption API, and `--e2ee-only` rejects plaintext
clients and legacy state. The default mixed mode remains for compatibility and
must not be described as an encrypted-only service. A future TLS/authenticated
registration design is needed before Internet deployment. All 3C.2A/3C.2B
cryptographic, storage rollback and hardware limitations remain.

### Actual verification

On 2026-10-30, Debug, Release and ASan/UBSan root builds each passed **16/16**
CTest entries, including the new five-case Python end-to-end suite using actual
separate C peer and Python relay processes. Existing pairing/persistence/Noise
IK suites passed **5/5 each** in all three configurations. The macOS LVGL simulator
also compiled (existing duplicate-library linker warning). After the final
transport deadline and test changes, both legacy and encrypted TCP integration
tests were rerun successfully in all three configurations. `git diff --check`
passed. Initial unprivileged socket tests were blocked by the sandbox and were
rerun with localhost permission; no blocked test is counted as a pass.

Verified process scenarios include:

- A enqueues while B is offline, exits, relay restarts with pending ciphertext,
  B connects later and decrypts/consumes, B exits, A reconnects and authenticates
  saved peer receipts. Two receipts are deliberately recovered in reverse order.
- A and B online with three-byte socket operations and a byte-fragmenting proxy
  that drops A halfway through its first upload. Reconnect/retry delivers the
  exact stored envelope; duplicate/reversed receipts leave completed state intact.
- Modified tags, unpaired sender identity and forged reverse receipts are routed
  through the real relay and rejected without inbox/outbox advancement.
- Full outbox rejects the ninth enqueue. A separately provisioned sender mailbox
  with the same paired identity sends a ninth distinct ID to a full recipient;
  no ACK is emitted and its outbox remains pending. Full relay backpressure is
  also tested, then capacity restoration recovers delivery.
- A recipient exits after durable application effect but before consume; restart
  verifies the same index entry and consumes without appending another effect.
- Relay files/logs contain neither the known plaintext marker (raw or hex) nor
  either private key; local mailbox/effect files contain no plaintext. Effect
  hashes are checked against the actual original application payload.

The tests are not Wi-Fi, ESP32-S3, power-cut, GUI-interaction or production
credential tests. Test/demo identity provisioning uses local 0600 fixture files;
production identity loading must use the established trusted pairing backend.

### Next milestone 3C.3

Define production-facing identity loading/rotation and authenticated relay
registration/TLS; choose safe bounded-history retention and epoch migration;
physically verify the C6 lab networking/mailbox and implemented NVS adapter
(the lab now cross-builds and passes host integration);
repeat hardware RNG, stack and persistence testing on the selected patched
ESP32-S3 platform. Integrate an application/UI consumer only after specifying
its idempotent durable effects and clear full-capacity/error behavior. Do not
promote this desktop prototype as a production Internet messaging system.


## LVGL persistent application integration (2026-10-31)

The simulator owns crypto/mailbox/link/TCP/history in a pthread, using the same
8-command/32-event atomic queues; LVGL applies at most four events per loop.
Encrypted durable history now restores incoming/outgoing text, sender/recipient,
direction, delivery and local read state. It saves outgoing intents before
mailbox enqueue, receives idempotently before consume, and archives exact wire
bytes/receipts before freeing completed active slots. No replay ID is discarded.
Eight active slots/direction and a 1024-total-ID archive bound resources; FULL is
explicit. Bounded archived-ACK scheduling retains fair retry without re-encryption.
The TCP adapter parses full input batches before reading more to support large
relay replays without spuriously disconnecting at 4096 bytes.

The explicit local snapshot v1 -> v2 migration binds an authenticated archive
UUID and grows 8376 -> 8392 bytes. It is crash-tested and fails closed on a wrong
or missing archive; old binaries cannot read v2. Legacy outgoing plaintext not
previously saved is explicitly unavailable, while its ciphertext and statuses
remain preserved. No E2EE wire change was required or made.

Local history uses existing AES-256-GCM/HKDF with a domain-separated key derived
from local private identity and paired public keys. Every write has a fresh RNG
nonce and authenticated namespace/header. Keys are not written separately, but
fixture private-key compromise exposes history. Production secure storage,
key rotation/backup, anti-rollback, hardware power-cut and wear tests are pending.
Full format/transaction/key assumptions are in `application-persistence.md`.

Normal and ASan/UBSan simulator suites passed 4/4: rotation, 100 messages per
direction, 41 crash boundaries, archive/full active capacity, corruption,
idempotence/exact replay, controller migration/errors, and real two-window
100-per-direction restoration with read/delivery state and relay reconnect.
Root normal and ASan/UBSan regressions passed 15/15 each. Startup and test commands
are in `simulator/README.md`. The relay still has a finite cached-frame limit;
incremental delivery now streams cached replay through the bounded per-client
output buffer. No unlimited retention/liveness claim is added. See
`../relay/README.md` for the 2026-11-03 relay-only verification.
