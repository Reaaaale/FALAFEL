# Falafel project status

Last updated: 2026-11-13

## Current status

2026-11-13 — Full GUI rotation now targets 280 ms with all 24 existing frames.
Absolute 11/12 ms deadlines, 5 ms refresh and an optimized LVGL build avoid the
previous unoptimized PNG/render bottleneck. A visible frame must be drawn before
the next index advances; slow rendering extends playback without skipping.
Normal and ASan/UBSan builds/full simulator suites pass 4/4 each. Timed normal
turns measured 280–281 ms from request; the final three snapshot-test samples
were exactly 280 ms. Sanitized drawing remains slower (~0.55–0.60 s), with no
sanitizer reports. Initial and round-trip pixels match each other and the prior
GUI. Left -1/right +1, personality navigation, current pose, cancellation and
busy-swipe policy remain unchanged. No new assets/dependencies/cache, no firmware,
relay, E2EE/TLS or persistent-data changes. See the simulator guide for timing
limits and manual visual checks.

2026-11-13 — Bidirectional GUI rotation corrected and software verified. Left
swipes decrement the frame index (-1 modulo 24); right swipes increment it (+1).
Personality navigation stays next/previous respectively. The current pose
survives completion/cancellation and starts the next animation without a reset.
The existing LVGL timer makes one full turn, then commits one personality change.
Busy mouse/trackpad gestures are dropped without queuing, including held drags
and inertial tails until the existing 220 ms quiet interval. No assets, firmware,
relay, E2EE, TLS or persistent data were changed. Normal simulator suite 4/4 and
all four sanitizer checks pass (GUI rerun with sanitizer-specific timing budget);
normal GUI smoke passes three successive runs. Actual rendered frame order,
nonzero-pose resume and callback counts are checked; left/right-return snapshots
match initial pixels exactly. Host C6/simulator E2EE and mTLS regressions 2/2 PASS.
Native macOS window startup/rendering was exercised. Physical trackpad settings/
momentum and S3 performance remain unverified. See [behavior and visual launch
commands](../simulator/README.md#character-and-interaction).

2026-11-12 — Optional mTLS is implemented for the Python relay, ESP32-C6
and macOS simulator. The existing TCP/E2EE bidirectional/reset milestone is
confirmed physically by the user and remains available. mTLS is software-verified
only: host normal/ASan/UBSan 20/20 each; simulator normal/sanitized 4/4 each;
17-case TLS suite and actual C6-source/SDL bidirectional E2EE/reopen integration
pass. New tests were rerun after final validation/CLI corrections. Fresh C6 TCP
and mTLS builds pass on IDF 5.5.6; mTLS image 877,600 B, app margin 105,440 B.
TLS CA/server/client keys are separate from existing E2EE identities. The relay
requires a trusted client certificate AND fingerprint-to-ID authorization before
session replacement. Both C clients verify the server CA/name; C6 certificate
validity checks require a trusted clock. Credentials are external; C6 app-flash
storage leaves the partition table and falafel_data unchanged. No GUI, E2EE,
mailbox-format or identity changes. No device flash, server deployment/access,
firewall/router/systemd change or real credential import was performed.

Pending: reviewed sidecar deployment and physical mTLS acceptance; measure
runtime heap/stack, clock bootstrap and reset behavior on hardware. No automatic
TCP fallback; no Secure Boot/Flash Encryption, automatic certificate renewal,
protected-key/production identity approval or authenticated SNTP. Cache retention,
NVS power-cut/GC and S3 validation remain open. See [mTLS architecture, software
results and exact manual deployment/hardware/rollback commands](mtls.md).

### Historical checkpoints (superseded by the current status above)

2026-11-11 — Relay stale-session reconnect corrected and software verified;
subsequent physical bidirectional/reset acceptance is confirmed by the user.

2026-11-08 — C6 lab runtime implemented, software verification complete;
**physical C6 -> Proxmox -> Mac milestone NOT COMPLETE**. Existing mailbox/link,
provider and NVS now run in the optional Wi-Fi worker: external lab identity
import, one durable preset/store, v2 frame parsing, authenticated receipts,
retry/reconnect, persistent incoming consumption and no new ID on reboot.
No Message Manager, GUI, TLS/mTLS, relay or wire changes. Local mailbox retains
eight lifetime IDs/direction without archive retirement. Host normal and
ASan/UBSan 19/19 each PASS, including C6 sources/NVS model with real relay and
SDL simulator, partial upload/reconnect/reopen and reverse preset. Provisioner
passes temporary-only tests. C6 IDF 5.5.6 binary 902,000 B, app free 81,040 B;
static DIRAM remaining 286,742 B is not measured runtime heap. No hardware flash,
real identity import or existing simulator-store changes. Proxmox deployment
was supplied (LXC 100, 10.77.0.2:8765), but Mac direct TCP timed out; LAN host
192.168.1.200:8765 was unreachable. Proposed host LAN proxy remains unapproved
and unapplied. Actual simulator paths/IDs and USB hardware run remain pending.
See [exact commands, limits and acceptance criteria](c6-relay-transport.md).

2026-11-06 — C6 LAN transport preparation implemented and cross-built. Optional
WPA2 Wi-Fi/FreeRTOS worker writes existing HELLO2 over bounded nonblocking TCP;
network defaults off, received bytes are discarded without ACK, and no identity,
mailbox messaging or crypto runtime is initialized. The storage probe remains
independent. RF-disabled crypto diagnostic, relay and wire formats are unchanged.
Desktop v2 exchange reproduced: host 17/17 normal and 17/17 ASan/UBSan, simulator
4/4 PASS; new host test exercises the same C TCP adapter against the real relay.
Fresh IDF 5.5.6 C6 builds PASS: disabled 189,472 B, enabled 882,640 B, leaving
100,400 B in the existing app partition. No credentials configured, flash or
hardware network test. Proxmox address/access not supplied: reachability and
compatibility remain unverified. Verified relay is temporary local loopback.
See [configuration, architecture and next steps](c6-relay-transport.md).

Relay cached delivery now uses ordered per-client pending-record keys and fills
the bounded output buffer incrementally after socket writes. The P1 226-frame
replay disconnect is fixed and host-verified (10 relay tests plus C transport
integration); GC, authentication and wire/state formats are unchanged. See
[relay behavior and tests](../relay/README.md).

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
permanent identities remain NO-GO. SEC-002 remains the last completed security
baseline milestone.

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
crypto approval remain pending. Default boot probes existing storage; the optional
C6 lab now has a temporary-identity mailbox worker with explicitly enabled initial
creation. Production enrollment and embedded history retirement remain pending.
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

- Opposite left/right cyclic GUI rotation with retained frame orientation,
  single deferred selection/callback and busy gesture/inertia rejection.

- Optional mTLS for relay/C6/simulator, explicit certificate fingerprint-to-ID
  authorization, bounded nonblocking handshakes and separate external TLS PKI.
  Existing plaintext TCP transport remains an explicitly selected lab mode.

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

- 2026-11-13, 280 ms timing: full simulator suites 4/4 normal and 4/4 ASan/UBSan
  PASS; all 24 draws and prior navigation/input/pose assertions retained. Direct
  normal timings 280–281 ms; final visual regression 280/280/280 ms. Sanitized
  timing is not a performance claim. Pixel equality and `git diff --check` pass.

- 2026-11-13, final direction inversion: normal and ASan/UBSan builds pass;
  circular_gui_smoke passes 1/1 each (14.85 s / 28.38 s). Frame order is left -1,
  right +1 modulo 24; personality destinations, current pose and input lock
  assertions remain unchanged. No other runtime logic was modified.

- 2026-11-13: expanded SDL/LVGL GUI smoke passes normal and ASan/UBSan, including
  both directions, left/right return, repeated/busy swipes and partial-pose resume.
  Normal scene snapshots before/after the round trip have identical pixels.

- 2026-11-12: mTLS software checks pass (20/20 host normal and sanitized,
  4/4 simulator normal and sanitized, C6 TLS/TCP cross-builds); actual hardware
  mTLS is NOT RUN. The existing TCP/E2EE hardware/reset milestone is user-confirmed.

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

Optional TLS wraps the existing framed transport below mailbox/link. The Python
relay uses SSLContext/DefaultSelector; C6 and simulator share the Mbed TLS client
adapter. Only authenticated/authorized registration can replace an mTLS owner.
The plain TCP mode, E2EE crypto and persisted data formats are unchanged. See
[the transport boundary and diagram](mtls.md#boundary-and-implementation).

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
  total IDs/device, with explicit FULL and no eviction. Relay cached delivery now
  streams incrementally through its bounded output buffer. At 100 messages/direction
  its 400 frames fit the default 512-entry cache; cache GC is not implemented.

- The 3C.1 channel requires an ordered live connection and closes on replay,
  reordering, authentication failure or disconnect; it cannot deliver offline.

- Legacy v1 mailbox users retain eight lifetime IDs/direction. Upgraded v2
  requires its matching archive; default APIs never delete replay history.
  Storage rollback/malicious edits are not prevented. Other external consumers
  still need durable sender/ID idempotence before consume.
- Plain TCP mode has unauthenticated routing. Optional mTLS authenticates the
  relay and certificate-authorized IDs but does not hide metadata from the relay.
  Use --e2ee-only as well as TLS for the encrypted lab instance. The headless
  mailbox_peer CLI remains a TCP test tool; C6 and GUI expose explicit TLS options.
- Physical mTLS, runtime resource margins, trusted time and protected keys are
  pending; keys remain extractable from firmware/files without Flash Encryption.
  Manual allowlist rotation/restart is implemented; automated renewal is not.
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
  circular UI uses one character for all personalities, a bidirectional 24-frame
  cycle with retained pose and an always-visible demo icon. PNG decode is synchronous per draw;
  slow platforms extend playback. ESP32-S3 flash/PSRAM and timing need validation.
- ESP-IDF storage adapter is implemented and C6 cross-built; physical NVS
  power-cut/GC/wear and S3 validation remain pending. The user reports successful
  physical C6 lab bidirectional E2EE and reset recovery; mTLS still needs
  separate deployment and physical acceptance. Other hardware/GUI adapters are not integrated. Embedded crypto remains blocked for real keys.
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
- Physical mTLS acceptance and production certificate lifecycle/protected-key hardening.
- Stage the independent mTLS relay and repeat physical C6 RESET/EN without
  restarting it; production identity/backup/key-rotation integration.
- Validate the implemented NVS adapter physically on C6, then cross-build/test
  on S3 with a verified crypto baseline; measure real capacity and wear.
- Validate storage wear, real power-cut recovery, archive/relay capacity and
  rollback-safe long-term retention; do not delete replay tombstones.
- Conversation retention/compaction policy.
- Benchmark rotation PNG decoding, flash/PSRAM usage and circular clipping on
  ESP32-S3; validate physical trackpad/touch input.
- Hardware board selection and official display/touch integration.

## Next recommended development step

Review [the separate mTLS deployment/hardware/rollback procedure](mtls.md).
Keep the current Proxmox 8765 service/proxy intact; stage an independent TLS
listener/cache on 8766 and use the documented temporary SSH forwarding for the
hardware trial. Reuse existing E2EE stores, change only the app firmware after
backup/authorization, and verify TLS identity, authenticated receipts, repeated
reset/reconnect and runtime heap/stack margins. mTLS physical acceptance is open.

Plan 3C.3 production identity loading/rotation, TLS certificate lifecycle and
safe retention/epoch design. Desktop durable history/consumption is implemented;
the software baseline is now validated. Next test runtime entropy and NVS
power cuts/GC on C6.
Use the now-validated complete 5.5.6 environment; next establish production
entropy ownership and protected identities with authorized disposable hardware
tests. Preserve identity NO-GO until the remaining SEC-001 gates are satisfied.
Then integrate protected identity/task/network adapters and validate S3,
stack/PSRAM, real capacity and endurance.

## Last completed milestone

2026-11-13 — 280 ms full-cycle target with all 24 frames, optimized bounded-memory
rendering, unchanged navigation/input/pose behavior and full software verification.
Physical trackpad/display timing remains a manual check.

2026-11-13 — Bidirectional cyclic GUI rotation and busy-gesture continuity
corrected, built and software-verified, with final left -1/right +1 mapping; physical trackpad review remains pending.

2026-11-12 — mTLS implementation and software verification complete for
relay/C6/simulator, retaining the user-confirmed TCP/E2EE/reset milestone.
Physical mTLS and deployment remain pending; see `mtls.md`.

2026-11-11 — Relay stale-session reconnect corrected and locally verified;
real bidirectional C6/Proxmox/Mac exchange confirmed by the user. Deployment and
hardware acceptance of the correction remain pending.

2026-11-06 — Registration-only C6 LAN transport preparation, both build variants
and desktop encrypted v2 regressions verified. Hardware/Proxmox remain pending.

2026-11-03 — Relay P1 incremental replay/backpressure. The 226-frame regressions
failed before the fix; 10/10 relay tests and 2/2 selected relay/C-transport CTest
entries now pass. No protocol, ACK, persistent-format or retention change.

### Previous security baseline

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


The initial circular simulator UI hosts LVGL 9.4 in a fixed 466 × 466 SDL2 window.
Its dark-sage (`#34483E`) screen contains a circular clipped content root shared by home and
message views. A build-generated image descriptor embeds
`simulator/assets/falafel.png`; LodePNG decodes it and the home scales uniformly
into a 320px stage. Cream (`#F3E9D8`) text and toasted-orange (`#D88A35`)
accents are applied with flat fills; the static PNG is unchanged. An accepted
pointer/trackpad swipe starts a 280 ms full-turn schedule over 24 compressed
read-only PNG descriptors, using absolute 11/12 ms LVGL timer deadlines. Left/next uses -1 and right/previous +1 modulo 24, starting from the
retained frame. A complete turn returns to that exact pose; completion and remote
cancellation no longer replace it with a still. Selection, indicators and one
callback commit only after the full turn. Busy gestures are discarded without
queuing, including pointer presses spanning completion and wheel tails through
the 220 ms quiet interval. The SDL watch uses a GUI-thread-published atomic busy
snapshot rather than inspecting LVGL objects. A remote selection cancels the
pending local choice without a callback and preserves the partial pose. Playback continues
when messages are open; their demo envelope and back button remain available.
The decoded-image cache is explicitly disabled, so drawing allocates only the
current decoded PNG rather than a 24-image set. PNG decoding is synchronous
per draw; no playback loop or sleep blocks the UI for the animation duration.
A 5 ms refresh period and `-O2` LVGL compilation support the 280 ms target even
in normal debug builds. Symbols/assertions and sanitizer instrumentation remain;
application/transport flags are unchanged. A draw-event guard prevents visible
frames from being skipped if rendering falls behind; playback then extends.
Rotation data occupies
2.72 MiB of read-only storage; one decoded frame is 848 KiB, plus decoder scratch
and clipping/display buffers. ESP32-S3 performance and allocation are unverified.
The rectangular status bar and composer have no entry point in this initial
layout. `--identity` and `--mailbox` explicitly select existing independent
stores; legacy `--data` is rejected. The controller initializes only transport
frame mode (HELLO2) and never submits events through falafel_app. That core stays
a link dependency of the transport's unused legacy persistence helpers.

```text
LVGL preset -> bounded SPSC commands -> pthread worker
  -> encrypted history intent -> active mailbox enqueue -> link v2 -> TCP -> relay
  -> peer worker -> authenticated durable inbox + saved peer receipt
  -> encrypted idempotent history effect -> consume -> archive -> active-slot release
  -> bounded ID-based events -> LVGL persistent-history upsert
```

One worker exclusively owns its crypto context, identity, mailbox and transport.
Only the existing DNS resolver runs separately. LVGL polls up to four events per
iteration without mutex waits. Initialization, filesystem writes/fsync, decrypt,
retry and close all run outside LVGL. A full event queue pauses only the worker;
stop releases that wait and drains accepted commands if storage remains usable.
Window-close and signals request stop while rendering continues, followed by
join after worker completion. Fatal storage state is not reset or reused.

The portable history store encrypts per-ID records and an authoritative manifest
using existing AES-GCM/HKDF APIs. It persists text, sender/recipient, direction,
delivery/local read state, original envelope and receipt. Receive effects commit
before consume; exact replay metadata commits before slot release. New durable
send intents can wait on disk when all eight active slots are occupied. Read and
delivery updates are monotonic/idempotent. GUI restoration/upserts use IDs, so
replay and state updates do not append duplicates. Rendering/layout is unchanged.

A random authenticated archive UUID binds the new local FMBX v2 snapshot to its
archive. Migration grows the snapshot 8376 -> 8392 bytes while preserving the
517-byte records; old binaries cannot read the upgraded state. Network v2 frames,
Noise K envelopes and relay implementation remain unchanged. History currently
retains 1024 IDs/device without eviction. Missing, corrupted or wrong archives
fail closed even with an initialization request. Production secure-key storage,
backup/rotation and ESP32-S3 durability/power-cut/wear validation remain pending.
See `application-persistence.md` for formats, transaction boundaries, key policy,
migration, measured memory/space and tests; `simulator/README.md` for commands.
