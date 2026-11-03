# Falafel project status

Last updated: 2026-11-03

## Current status

Revalidation on 2026-11-03: fresh C6/native/sanitized builds and offline link
checks PASS in `build/sec003-20261103/`; previous artifacts retained.

SEC-003 **software preparation is verified; hardware validation NOT RUN / OPEN**.
The existing C6 crypto experiment now exercises the real provider, temporary
X25519/ECDH, HKDF/GCM vectors and tampering, observed automatic reseed, injected
entropy failures and cleanup. Clean C6 build passes (248,208 bytes); focused
native suites pass 3/3 normal and 3/3 ASan/UBSan, including 14 synthetic log-parser
cases. These are software results, not physical evidence. Experiment-only
callback instrumentation forwards successful calls to the real SDK; production
provider/protocol/storage are unchanged. See [SEC-003](security/SEC-003.md).
Next: manually collect/review five warm and five cold C6 runs using that report.
RF/ADC/sleep entropy integration and protected-key validation remain pending;
permanent identities remain NO-GO. SEC-002 remains the last completed milestone.

SEC-002 validates isolated **ESP-IDF 5.5.6 / Mbed TLS 3.6.7 as the C6
development baseline: GO**. Official source/fix ancestry, actual compilation
configuration and linked corrected Curve25519 reduction are verified. Clean
firmware/crypto builds, 16/16 normal and sanitized host tests and 4/4 simulator
tests pass. The old 5.5.2 SDK is retained unchanged. **Permanent real identities
remain NO-GO on C6/S3** pending device entropy and protected-key validation.
No provider/protocol/format/pairing or EMB-002 edits. See
[SEC-002](security/SEC-002.md); [SEC-001](security/SEC-001.md) records old-baseline
findings. No flash or S3 build was performed.

The ESP-IDF persistence increment is implemented: a dedicated NVS backend
implements existing mailbox/history callbacks with full-ID slot mapping and
fail-closed errors. Normal and ASan/UBSan host suites pass 16/16 each; the C6
firmware cross-build passes. Physical power-cut/GC/capacity, S3 and embedded
crypto approval remain pending. Boot probes existing storage only; there is no
firmware messaging consumer, enrollment or automatic store creation.
See [embedded storage](embedded-storage.md).

Milestone 3C.1 is an isolated desktop live-channel prototype: Noise IK uses
persisted paired identities and directional authenticated cipher states. It is
not an asynchronous messaging protocol or connected to event-v1.

Milestone 3B.3 is complete as a desktop prototype: local X25519 identities and
bilaterally verified peer keys survive process restarts, with private keys held
in macOS Keychain and public metadata written atomically. The module remains
isolated from the application protocol, so Falafel is not yet a secure Internet
messaging system.

Milestone 3C.2A is complete as an isolated portable offline encryption module:
Noise K authenticated envelopes, using the shared provider and persisted pairing
identities. The headless desktop path is now integrated by 3C.2C. See
`docs/async-e2ee-design.md` for format, verification and security limits.

Milestone 3C.2B is complete as an isolated persistent mailbox: portable C11 state,
POSIX atomic backend, durable deduplication and peer-authenticated acceptance
receipts. Delivery to external application effects requires the documented
peek/consume and idempotent consumer contract; exactly-once effects are not claimed.

Milestone 3C.2C is complete as a headless desktop integration: portable v2 link,
existing TCP adapter, durable Python ciphertext relay and idempotent minimal
application consumer. Actual offline handoff and saved peer-ACK recovery pass
with A, B and relay in separate processes. The LVGL simulator now reuses this
E2EE path in a dedicated worker;
its application history now commits before consume, then archives replay
IDs and exact receipts before reusing active slots.

The first circular simulator UI is implemented: fixed 466 × 466 SDL2/LVGL 9.4
window, supplied PNG embedded at build time, clipped circular dark-sage viewport,
mouse/trackpad personality selection with 24-frame prerendered rotation, and
real mailbox navigation and preset E2EE sending. The controller changes no
cryptography, relay implementation, protocol or mailbox format.

## Implemented features

- Isolated official IDF 5.5.6 tools/venv, pinned child-process wrapper and
  reproducible SEC-002 source/configuration/ELF evidence verifier. Application
  sources and existing generated configs remain unchanged.

- ESP-IDF NVS mailbox/history backend and portable persistence build components;
  30 modeled interruption/error scenarios, 100 receive/reopen cycles, corruption,
  logical full capacity and no automatic erase/reset. No protocol/format change.

- Encrypted v2 TCP data/receipt frames, fair retry scheduling, reconnect recovery,
  relay ciphertext/receipt persistence and durable application hash-index consumer.
- Persistent encrypted mailbox with eight active slots/direction, exact-byte
  retry, atomic receipts and authenticated durable-acceptance ACKs. Legacy v1
  keeps lifetime slots; the simulator upgrades to archive-bound local v2.
- Portable offline Noise K envelopes: 122-byte plaintext / 256-byte envelope.
- Portable C11 application state and bounded queues.
- LVGL E2EE controller with pthread ownership, 8-command/32-event atomic queues,
  strict v2 transport, existing-store CLI, fail-closed startup and graceful stop.
- AES-GCM application history persists sent/received text, IDs, sender/recipient,
  direction, delivery and local reads. Idempotent history precedes consume;
  archived IDs/receipts precede slot release. Current limit: 1024 total IDs/device.
- LVGL/SDL desktop simulator with messages, three personalities and timer-driven
  24-frame rotation; compressed assets in read-only data, decoded cache disabled.
- Non-blocking TCP client including asynchronous DNS resolution.
- Durable Python relay with offline delivery and final-status recovery.
- At-least-once transport delivery with bounded application deduplication.
- Persistent non-reused event-ID ranges.
- Versioned, checksummed desktop state.
- Deterministic personality synchronization and safe unsent coalescing.
- Portable Mbed TLS provider: CTR-DRBG (platform entropy required), X25519, HKDF-SHA256, AES-256-GCM,
  constant-time comparison and secure erasure.
- Standalone ESP32-C6 cryptographic test application.
- Isolated Noise-C XX/AES-GCM feasibility harness and ESP32-C6 cross-build.
- Portable initiator/responder pairing state machine, uniform 12-digit SAS,
  authenticated bilateral confirmation, and two-process desktop harness.
- Portable persistent pairing state/codec plus macOS Keychain and atomic-file
  backend, explicit unpair, and optional local identity rotation.

## Verified functionality

- SEC-002: clean C6 firmware/crypto builds (0x2e390 / 0x3b810), normal host
  16/16, ASan/UBSan 16/16 (vendor also instrumented), simulator 4/4 using the
  candidate SDK's actual Mbed TLS fork and isolated Python. Hardware/identity
  approval is not inferred from functional PASS.

- 2026-11-01: root tests 16/16 normal and 16/16 ASan/UBSan, including the actual
  ESP-IDF adapter against an explicitly limited NVS API model. C6 build succeeds
  with existing IDF 5.5.2; binary 0x2d660. No physical flash/power-cut or S3 run.

- Persistent simulator: normal and ASan/UBSan builds and suites passed 4/4 on
  2026-10-31. Actual two-window tests sent 100 messages each direction and restored
  200 rows/device, including outgoing/incoming delivery and local read states.
  Verified relay offline/restart/replay, no duplicate rows, migration, missing/
  corrupt/different archives, queues and runtime storage failure. History tests
  killed processes at 41 write/effect/consume/retire/migration boundaries and
  checked active/archive FULL and exact old receipts. Existing root normal and
  ASan/UBSan suites passed 15/15 each. Maximum 100-message GUI loop gaps: 24 ms
  normal, 63 ms instrumented. Worker/history 93,376 bytes; 200-record history
  163,460 logical bytes plus 8,392-byte mailbox. See application-persistence.md.
  Hardware, physical trackpad, real power cuts and flash endurance unverified.

- 3C.2C: root Debug/Release/ASan+UBSan suites passed 16/16 each, with five actual
  TCP subprocess scenarios covering offline sender/recipient handoff, relay
  restart, saved/reordered/duplicate receipts, fragmented disconnect, forged
  payloads, capacity and consumer crash. Pairing/IK suites passed 5/5 each;
  desktop simulator compiled. Final TCP changes were reverified in all builds.

- 3C.2B: Debug, Release and ASan/UBSan root suites passed 15/15 each, and
  pairing/persistence/IK regressions passed 5/5 each. Mailbox tests include 28
  process-crash transaction scenarios, consumer-ledger recovery, storage errors,
  corruption, full capacity, restart deduplication and invalid/duplicate ACKs.

- 3C.2A: root Debug, Release and ASan/UBSan suites passed 14/14 each, including
  offline encryption, negative cases, exhaustive provider-call fault injection,
  Noise-C interoperability and relay/transport regressions. Pairing/IK regression
  suites passed 5/5 each in all three configurations on 2026-10-30.

- Core and protocol unit tests under ASan/UBSan.
- Relay persistence failure, restart, backpressure and fragmented I/O tests.
- Desktop application/transport/relay round trip with no duplicate application
  delivery.
- Corrupt and unsupported desktop and relay state rejection.
- Desktop simulator compilation on macOS.
- Official RFC 7748, RFC 5869 and AES-GCM vectors plus crypto negative tests.
- ESP32-C6 cross-build with ESP-IDF 5.5.2 after moving the 1,536-byte crypto
  context from the main-task stack to internal heap (application binary
  `0x3a100` bytes).
- Physical ESP32-C6-DevKitC-1 execution completed without a panic: all
  cryptographic self-tests passed and `app_main()` returned normally.
- Noise-C `cfe2541` completed XX handshakes on Apple Silicon, passed positive
  and negative tests under ASan/UBSan, and passed its upstream Cacophony vector
  for `Noise_XX_25519_AESGCM_SHA256`.
- The standalone Noise experiment cross-built with ESP-IDF 5.5.2; its physical
  run completed 100 handshakes with PASS and no watchdog warning after the
  benchmark correction.
- Desktop pairing positive and negative tests cover bilateral completion, SAS
  mismatch, explicit rejection, modified handshake/confirmation records,
  missing confirmation, premature decisions, duplicates, and terminal reuse.
- The 3B.2 test target builds and executes those checks in both Debug and
  Release; `NDEBUG` is explicitly undefined for the assertion-based test only.
- Persistent-state tests cover first identity creation, restart recognition,
  completed-pairing commit, corruption, interrupted atomic replacement,
  incomplete-session restart, explicit unpair, and identity rotation.
- Consolidated clean builds fetch Noise-C into their own build directory from
  revision `cfe2541` with the pinned archive SHA-256; no `/tmp` source is
  required. Debug, Release, Noise and sanitizer suites pass from clean trees.

CI is configured for GCC, Clang, sanitizer tests and an Ubuntu simulator build;
the Milestone 2 workflow passed. The current uncommitted Milestone 3A changes
have been verified locally but have not yet run in GitHub Actions.

## Current architecture

See `docs/architecture.md`. Legacy test application events pass through the portable core, versioned
protocol, desktop transport and durable relay. The interactive simulator uses
the encrypted mailbox/link through a dedicated worker instead. Platform persistence
and networking remain outside the shared core.
The encrypted headless path uses the portable mailbox/link, existing desktop
TCP transport and Python relay v2 cache. It does not layer legacy application
event ACK/retry rules over mailbox state. Pairing and live IK remain independent;
the LVGL simulator uses strict v2 with no legacy messaging fallback. Its GUI
thread only submits commands and projects copied events; the worker owns all
crypto/mailbox/history/network state and performs synchronous persistence
off-thread. History stores durable send intents before enqueue and idempotent
receive effects before consume; replay archive persists before active-slot reuse.

## Known bugs and limitations

- Desktop history is persistent/encrypted, but its key derives from the existing
  fixture private key. Production secure enrollment/key storage/rotation and
  backup/export are not integrated. Local rollback/media-loss resistance is not
  claimed. Legacy outgoing text never stored by v1 cannot be recovered and is
  explicitly marked unavailable at migration. Local reads are not peer receipts.
- Active mailbox capacity is eight per direction; history retains up to 1024
  total IDs/device, with explicit FULL and no eviction. Relay cache/output replay
  limits are independent. At 100 messages/direction its 400 frames fit the default
  512-entry cache; unlimited lifetime relay replay/GC is not implemented.

- The 3C.1 channel requires an ordered live connection and closes on replay,
  reordering, authentication failure or disconnect; it cannot deliver offline.

- Legacy v1 mailbox users retain eight lifetime IDs/direction. Upgraded v2
  requires its matching archive; default APIs never delete replay history.
  Storage rollback/malicious edits are not prevented. Other external consumers
  still need durable sender/ID idempotence before consume.
- Legacy v1 test traffic is cleartext. GUI/headless v2 content is E2EE, but TCP
  registration/routing remain unauthenticated and lack TLS. Use --e2ee-only for
  the encrypted relay instance; default mixed mode also accepts legacy traffic.
- The v2 relay cache retains data and receipts without automatic reclamation.
  Configure capacity for both; authenticated epoch/compaction is future work.
- Deduplication history is bounded to 64 device identities and 1,024 relay
  completions; very old replays may be accepted after eviction.
- A full receiving inbox rejects additional text until a future retention/user
  history policy is implemented.
- Desktop state format v1 is rejected rather than migrated because it lacks a
  trustworthy ID high-watermark.
- Atomic rename/fsync cannot guarantee survival against every storage device,
  filesystem or hardware failure.
- GUI smoke and two-window E2EE automation cover rotation, messages and shutdown;
  broader interaction/accessibility and physical trackpad tests remain pending. The
  circular UI uses one static PNG for all personalities, a shared 24-frame
  rotation and an always-visible demo icon. PNG decode is synchronous per draw;
  slow platforms extend playback. ESP32-S3 flash/PSRAM and timing need validation.
- ESP-IDF storage adapter is implemented and C6 cross-built; physical NVS
  power-cut/GC/wear and S3 validation remain pending. Other hardware/network/GUI
  adapters are not integrated. Embedded crypto remains blocked for real keys.
- Pairing and identity persistence are implemented only as a desktop prototype;
  production identity loading is not implemented. GUI integration uses trusted
  fixture identity files generated explicitly; neither GUI nor headless runtime
  is connected to the Keychain/SAS enrollment lifecycle. Mailbox retains replay
  history with fixed lifetime capacity; the raw envelope primitive accepts replay.
  Offline envelopes rely on probabilistic CSPRNG ephemeral uniqueness.
- Metadata checksum and atomic replacement do not prevent rollback to an older
  valid file. The generation counter has no separately trusted monotonic copy.
- ESP-IDF 5.5.2 contains `mbedtls-3.6.5-11-gffb280bb63`. Inspection of its
  bundled Mbed TLS history confirms that its Espressif commits do not include
  the ECC side-channel correction released in Mbed TLS 3.6.7. X25519 is
  therefore still unsafe in that retained old installation. SEC-002 validates
  corrected 5.5.6 separately for C6 development; production identities still
  require hardware entropy/key-protection validation.
- SEC-001 confirms this is CVE-2026-54435, not an unresolved version guess.
  Official 5.5.6 uses corrected 3.6.7; 5.5.5 still uses uncorrected 3.6.6.
  The final application's entropy source must also remain valid at seed/reseed.
  The isolated C6 test now owns non-RF entropy, but has only been cross-built.
  No permanent-key approval follows from functional PASS.
- The original ESP32-C6 hardware run exposed a main-task stack protection
  fault. Allocating the 1,536-byte crypto context from internal heap fixed it
  without increasing the 3,584-byte task stack. The final physical run retained
  1,952 bytes at the minimum stack high-watermark, passed the one-block NIST
  AES-256-GCM test and returned normally from `app_main()`.
- The final one-shot hardware run reported 472 fewer free heap bytes after
  cleanup. This small delta can be caused by lazy ESP-IDF/Mbed TLS runtime
  allocation, but repeated in-process initialization is not exercised and
  would need a dedicated leak/stability test before making the self-test
  recurrent.
- Noise-C has no tagged release, reports version 0.0.1, and its latest official
  commit is from 2023-12-06. Its reference backend duplicates the provider's
  primitives, uses dynamic allocation without allocator injection, and needs a
  platform RNG adapter. It is not approved for production integration yet.
- The 3B.2 pairing module still uses Noise-C's reference backend and dynamic
  allocation; embedded integration has not been attempted.

## Pending tasks

- Milestone 3C.2 desktop prototype is complete (A/B/C). 3C.3 must address
  production identity/relay authentication, retention/epochs and hardware
  adapters. See `docs/async-e2ee-design.md`.

- Decide whether a small Mbed TLS/provider backend for the Noise-C state machine
  is maintainable, then repeat vectors for that backend.
- Define authenticated retention/epochs and persistence rollback/replay policy.
- Production relay authentication and transport security.
- ESP-IDF networking/worker and secure identity/backup/key-rotation integration.
- Validate the implemented NVS adapter physically on C6, then cross-build/test
  on S3 with a verified crypto baseline; measure real capacity and wear.
- Validate storage wear, real power-cut recovery, archive/relay capacity and
  rollback-safe long-term retention; do not delete replay tombstones.
- Conversation retention/compaction policy.
- Benchmark rotation PNG decoding, flash/PSRAM usage and circular clipping on
  ESP32-S3; validate physical trackpad/touch input.
- Hardware board selection and official display/touch integration.

## Next recommended development step

Plan 3C.3 production identity loading/rotation, relay authentication/TLS and
safe retention/epoch design. Desktop durable history/consumption is implemented;
the software baseline is now validated. Next test runtime entropy and NVS
power cuts/GC on C6.
Use the now-validated complete 5.5.6 environment; next establish production
entropy ownership and protected identities with authorized disposable hardware
tests. Preserve identity NO-GO until the remaining SEC-001 gates are satisfied.
Then integrate protected identity/task/network adapters and validate S3,
stack/PSRAM, real capacity and endurance.

## Last completed milestone

2026-11-02 — SEC-002 isolated ESP-IDF 5.5.6 validation: development GO for C6,
permanent identities NO-GO. Source/fix/config/link evidence and clean builds
verified, host normal/sanitized 16/16 and simulator 4/4 pass. Old SDK/configs
preserved; EMB-002 excluded; no flash or application/protocol changes.

### Previous security assessment

2026-11-02 — SEC-001 assessment, with NO-GO and partial test-harness remediation.
Effective versions/patches/configuration and relevant CVEs verified; host normal
and sanitizer regressions 16/16, C6 firmware/crypto builds pass. The official SDK
upgrade, runtime hardware/entropy and protected-identity validation remain open.

### Previous persistence increment

2026-11-01 — ESP-IDF mailbox/encrypted-history NVS backend.
Existing formats and shared core unchanged; exclusive owner, explicit namespace
creation, full-ID slot mapping and poisoned-error recovery implemented. C6
cross-build and host normal/sanitized 16/16 suites pass. Physical power-cut/GC
verification and production crypto/identity protection remain pending.

### Previous milestone

2026-10-31 — Desktop encrypted application history and safe mailbox retirement.
The underlying network foundation remains Milestone 3C.2C.

Implemented encrypted persistent application history, safe consumption and
archive-backed active-slot reuse. Text, ID, sender/recipient,
direction, delivery and local read states restore in the unchanged circular GUI.
Local AES-GCM/HKDF reuse the provider; an authenticated UUID binds archive and
mailbox. Explicit v1 -> v2 snapshot migration is tested, including interruption.
Eight slots/direction are reusable; 1024 total archived IDs/device are retained
without eviction. Network E2EE protocol and relay implementation are unchanged.
Normal and ASan/UBSan simulator suites passed 4/4, including 100 messages each
direction and 41 crash boundaries; existing root suites passed 15/15 each.
Remaining: physical storage/GC validation, protected identity/FreeRTOS/network
integration and S3 PSRAM/stack/endurance tests. See embedded-storage.md and
application-persistence.md.
