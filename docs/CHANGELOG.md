# Falafel development changelog

## 2026-10-27 — Milestones 3B.1–3B.3 consolidation

### Components modified

- Shared pinned Noise-C CMake declaration and the three 3B build targets.
- Ignore rules and reproducible build/test documentation.

### Changes and verification

- Centralized revision `cfe25410979a87391bb9ac8d4d4bef64e9f268c6` and archive
  SHA-256 verification. Clean builds now fetch under their build directories;
  `NOISE_C_SOURCE_DIR` is only an optional offline override.
- Added explicit ignore patterns for pairing state and atomic temporary files.
- Removed literal private-key test fixtures from the feasibility harness; each
  side now generates its distinct test identity with Noise-C's platform RNG.
- Clean Noise Debug and ASan/UBSan suites: 1/1 passed in each.
- Noise-C upstream `make check` unit and vector suite passed from the fetched
  pinned source tree, including the selected XX/25519/AES-GCM/SHA-256 vector.
- Clean pairing Debug, Release, and ASan/UBSan suites: 4/4 passed in each,
  including Keychain restart/persistence and all negative tests.
- Existing normal and sanitizer regressions: 11/11 passed in each.
- Repository scan found no committed local state, private key, or credential.

## 2026-10-27 — Milestone 3B.3 persistent desktop pairing

### Components modified

- Portable persistent pairing state, validation, and metadata codec.
- macOS Keychain/atomic-file storage backend and restart harness.
- Pairing API peer-key export restricted to bilaterally completed sessions.
- Desktop pairing build, tests, architecture, status, protocol, and README.

### Features implemented and bugs fixed

- Local identities are created before pairing and reused across restarts;
  private keys remain in Keychain and are never written to metadata or logs.
- Verified peer keys are committed only from `COMPLETE`; rejected and
  incomplete sessions cannot export a persistable peer.
- Atomic `0600` metadata replacement uses random same-directory temporary
  files, file/directory `fsync()`, and rename. Corrupt state fails closed and
  explicit unpair supports identity rotation.
- Fault injection exposed and fixed temporary-name collision after an
  interrupted save by replacing PID-derived names with `mkstemp()`.

### Tests performed

- Fresh AppleClang Debug, Release, and ASan/UBSan builds: 4/4 CTest targets
  passed in each configuration.
- Covered codec corruption/truncation/key mismatch, interrupted replacement,
  process restart, peer recognition, incomplete pairing restart, unpair, and
  identity rotation using temporary real macOS Keychain items.
- Existing repository and Noise feasibility regressions: passed.

### Architectural decisions and limits

- Filesystem operations are outside the portable state module. Keychain holds
  the only at-rest copy of the local private key; the file is public metadata.
- CRC32 and atomicity detect corruption/torn writes but cannot prevent replay
  of an older valid metadata file. No anti-rollback guarantee is claimed.

## 2026-10-27 — Milestone 3B.2 desktop Noise XX pairing

### Components modified

- Portable `shared/pairing` API and implementation.
- Standalone macOS pairing build, interactive peer, two-process harness, and
  pairing tests.
- README, architecture, protocol and project-status documentation.

### Features implemented and bugs fixed

- Complete initiator/responder Noise XX exchange with distinct X25519
  identities using the Noise-C revision pinned by 3B.1.
- Domain-separated SHA-256 SAS derivation with 40-bit rejection sampling and a
  zero-padded 12-digit display.
- Explicit state enforcement plus transcript-, SAS-, and role-bound decisions
  authenticated with the Noise `Split()` cipher states; completion requires
  both accepts.
- The corrected ESP32-C6 benchmark completed 100 handshakes with PASS and no
  watchdog warnings.
- Fixed the 3B.2 Release test build by explicitly keeping assertions enabled
  for `test_pairing`; Release optimization and `-Werror` remain enabled, and
  no positive or negative check is compiled out by `NDEBUG`.

### Tests performed

- AppleClang debug build and CTest pairing unit/two-process suite: passed.
- Positive completion and negative SAS mismatch, rejection, altered handshake,
  altered confirmation, missing confirmation, premature confirmation,
  duplicate and terminal-session reuse cases: passed.
- ASan/UBSan pairing unit and two-process tests: 2/2 passed.
- Existing normal and ASan/UBSan repository suites: 11/11 passed in each build;
  the 3B.1 Noise feasibility regression also passed.
- Fresh Debug and Release 3B.2 builds: 2/2 CTest targets passed in each; the
  Release compile flags contain `-DNDEBUG` followed by `-UNDEBUG`, and the
  standalone negative-test executable passed with its assertions active.

### Architectural decisions

- Pairing is a portable module independent of GUI and transport; the desktop
  programs supply length-framed sockets only.
- No GUI, relay, event-v1, 3A provider, persistence, or application E2EE change
  is part of this milestone.

## 2026-10-26 — Milestone 3B.1 Noise feasibility gate

### Components modified

- Isolated host Noise-C experiment and selected-suite build.
- Standalone ESP32-C6 Noise experiment with platform RNG adapter and metrics.
- Project status and experiment documentation.

### Features implemented

- Complete `Noise_XX_25519_AESGCM_SHA256` exchange with distinct static keys,
  handshake-hash agreement, Split transport proof, bidirectional encryption,
  fresh ephemerals, and negative protocol/AEAD/state tests.
- Reproducible Noise-C commit and archive hash pinning without changing the 3A
  provider or application architecture.

### Tests performed

- AppleClang 21 release build: experiment passed; 100-handshake mean 204.22 us.
- AppleClang 21 ASan/UBSan build: passed.
- Noise-C upstream unit and vector suite: passed, including the independent
  Cacophony vector for the selected suite.
- ESP-IDF 5.5.2 / RISC-V GCC 14.2.0 cross-build: passed; binary `0x2af20`,
  total image content 175,796 bytes. Flash and physical execution not run
  because no serial device was visible.

### Architectural decisions

- Decision is NEED FURTHER EVALUATION, not adoption: the state machine is a
  viable candidate, while the stale, dynamically allocated reference backend
  duplicates the existing X25519, AES-GCM, and SHA-256 implementation.
- A provider/Mbed TLS backend experiment and physical C6 measurements are the
  gate before Milestone 3B.2. No custom Noise XX implementation was created.

## 2026-10-26 — ESP32-C6 crypto experiment stack fix

### Components modified

- ESP32-C6 crypto experiment entry point and sdkconfig defaults.
- ESP32-C6 experiment and project-status documentation.

### Bugs fixed

- Removed the 1,536-byte opaque crypto context from the 3,584-byte main-task
  stack and allocated it from internal 8-bit-capable heap instead.
- Preserved the configured main-task stack size rather than hiding the fault
  with an arbitrary stack increase.
- Unified self-test cleanup and final reporting so each boot produces exactly
  one clear `PASS` or `FAIL` result.

### Tests performed

- ESP32-C6 cross-build with ESP-IDF 5.5.2 and GNU RISC-V GCC 14.2.0.
- Disassembly check: 32-byte `app_main()` frame and 336-byte
  `known_vector_tests()` frame after the fix.
- Final physical ESP32-C6-DevKitC-1 run passed all cryptographic self-tests and
  returned normally from `app_main()` with Mbed TLS 3.6.5.
- Final measurements: 651,170 us elapsed; 3,584-byte configured main stack;
  1,952-byte minimum free stack; 460,768/460,296 bytes free heap before/after;
  434,176-byte largest free block; and `0x3a100`-byte application binary.
- All 11 desktop CTest targets normally and under ASan/UBSan, including crypto
  vectors, relay integration and transport end-to-end tests.

### Architectural decisions

- Long-lived opaque cryptographic contexts belong in explicitly managed heap
  storage on ESP-IDF; small fixed known-answer buffers remain local.
- FreeRTOS stack high-watermarks are logged around initialization, vector tests
  and cleanup so later growth is measurable.
- The C6 hardware self-test uses the NIST AES-256-GCM one-block vector because
  ESP-IDF 5.5.2's accelerated backend rejects zero-length input. The desktop
  suite retains coverage of the empty-plaintext vector.
- The shared crypto provider remains unchanged.

The stack correction and the subsequent one-block GCM self-test binary were
both confirmed on hardware. The 1,952-byte minimum free stack gives adequate
headroom for this bounded, one-shot experiment without increasing the task
stack. The 472-byte heap delta is recorded for future repeated-initialization
testing; it does not prevent the current one-shot program from completing.

The ESP-IDF dependency identifies itself as
`mbedtls-3.6.5-11-gffb280bb63`. Its local history does not contain the ECC
side-channel correction released in Mbed TLS 3.6.7, so this result validates
X25519 functionality but does not approve that dependency for production use
with persistent private keys.

## 2026-10-26 — Milestone 3A cryptographic foundation

### Components modified

- Shared portable crypto API and Mbed TLS provider.
- Root CMake, desktop tests and standalone ESP32-C6 experiment.
- Architecture, protocol, status and build documentation.

### Features implemented

- Secure RNG, X25519, HKDF-SHA256 and AES-256-GCM behind a C11 API without
  public Mbed TLS types.
- Constant-time equality, best-effort zeroization and explicit authentication
  failure handling.
- Official vectors, tamper/length/buffer tests and a C6 serial self-test with
  timing and heap measurements but no secret logging.

### Tests performed

- Host crypto and existing portable targets built with AppleClang.
- Crypto, core, protocol and persistence CTests passed.
- ESP32-C6 cross-build passed with ESP-IDF 5.5.2.
- All 11 CTest targets passed normally and under ASan/UBSan, including relay
  fault injection and transport end-to-end tests.

### Architectural decisions

- AES-256-GCM is used because C6/S3 support AES acceleration and ESP-IDF enables
  GCM without optional ChaCha/Poly1305 configuration.
- Desktop Mbed TLS is pinned to official 3.6.7 with archive hash verification.
- Runtime event v1, ACK semantics and GUI remain unchanged.

## 2026-10-26 — GCC CI warning fix

### Components modified

- Event protocol serialization formatting.

### Bugs fixed

- Removed two misleading-indentation diagnostics reported by GCC with
  `-Werror`, preserving the existing serializer and deserializer logic.

### Tests performed

- Complete 10-test host suite.
- Complete 10-test host suite with AddressSanitizer and
  UndefinedBehaviorSanitizer.
- Core and simulator builds with `-Werror -Wmisleading-indentation` under
  AppleClang.
- Full simulator build.

The local macOS environment does not provide GNU GCC or a Linux container, so
the final Ubuntu/GCC confirmation remains the next GitHub Actions run.

## 2026-10-26 — Reliability Hardening

### Components modified

- Portable application core and event validation.
- Desktop transport, argument handling and persistence.
- Python relay persistence and socket output handling.
- Simulator status integration.
- CMake, CI and automated tests.

### Features and fixes

- Made relay acceptance conditional on successful durable persistence.
- Added durable completed-delivery records and sender status recovery.
- Added explicit queue-full backpressure without eviction.
- Added persistent event-ID range reservation.
- Replaced raw C state dumps with versioned, fixed-width, checksummed encoding.
- Added application origin validation and idempotent acknowledgements.
- Added bounded durable received-message and deduplication state.
- Coalesced personality events only before first transport submission and
  preserved submitted or accepted events.
- Made DNS, connect, reads and writes non-blocking on desktop.
- Replaced relay `sendall()` with bounded selector-driven output buffers.
- Corrected the status bar to indicate relay connectivity rather than peer
  presence.

### Tests performed

- 10 CTest targets with AddressSanitizer and UndefinedBehaviorSanitizer.
- Protocol, application, persistence and malformed-state unit tests.
- Relay fault injection, restart, backpressure and fragmented I/O tests.
- C application-to-application integration through both desktop transports and
  the Python relay.
- Desktop simulator build and Python syntax validation.

### Architectural decisions

- Delivery is at-least-once with bounded application deduplication.
- The relay retains pending data until a durable recipient ACK transition.
- Relay-reported final status is accepted as recovery of a previously validated
  peer ACK, not as E2E authentication.
- Unsafe automatic migration from persistence v1 is rejected explicitly.
