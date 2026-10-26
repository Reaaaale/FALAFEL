# Falafel development changelog

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
