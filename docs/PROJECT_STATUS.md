# Falafel project status

Last updated: 2026-10-26

## Current status

Milestone 2 remains implemented and Milestone 3A now provides a tested portable
cryptographic foundation. It is intentionally not integrated into messages, so
Falafel is not yet a secure Internet messaging system.

## Implemented features

- Portable C11 application state and bounded queues.
- LVGL/SDL desktop simulator with messages and three personalities.
- Non-blocking TCP client including asynchronous DNS resolution.
- Durable Python relay with offline delivery and final-status recovery.
- At-least-once transport delivery with bounded application deduplication.
- Persistent non-reused event-ID ranges.
- Versioned, checksummed desktop state.
- Deterministic personality synchronization and safe unsent coalescing.
- Portable Mbed TLS provider: secure RNG, X25519, HKDF-SHA256, AES-256-GCM,
  constant-time comparison and secure erasure.
- Standalone ESP32-C6 cryptographic test application.

## Verified functionality

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

CI is configured for GCC, Clang, sanitizer tests and an Ubuntu simulator build;
the Milestone 2 workflow passed. The current uncommitted Milestone 3A changes
have been verified locally but have not yet run in GitHub Actions.

## Current architecture

See `docs/architecture.md`. Application events pass through the portable core,
versioned protocol, desktop transport and durable relay. Platform persistence
and networking remain outside the shared core.
The shared crypto provider is currently isolated from that event path.

## Known bugs and limitations

- Transport and relay are cleartext and unauthenticated.
- Deduplication history is bounded to 64 device identities and 1,024 relay
  completions; very old replays may be accepted after eviction.
- A full receiving inbox rejects additional text until a future retention/user
  history policy is implemented.
- Desktop state format v1 is rejected rather than migrated because it lacks a
  trustworthy ID high-watermark.
- Atomic rename/fsync cannot guarantee survival against every storage device,
  filesystem or hardware failure.
- GUI interaction itself is not automated; integration coverage stops at the
  application/transport boundary.
- ESP32-S3 adapters and hardware integration are not implemented.
- Pairing, key persistence, nonce allocation, replay state and an encrypted
  envelope are not implemented.
- ESP-IDF 5.5.2 contains `mbedtls-3.6.5-11-gffb280bb63`. Inspection of its
  bundled Mbed TLS history confirms that its Espressif commits do not include
  the ECC side-channel correction released in Mbed TLS 3.6.7. X25519 is
  therefore approved here only for functional experimentation; production use
  with persistent private keys must move to a verified Espressif release that
  carries the correction and must repeat the hardware tests.
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

## Pending tasks

- Authenticated pairing, persistent keys and E2E envelope integration.
- Production relay authentication and transport security.
- ESP-IDF networking and NVS persistence adapters.
- Conversation retention/compaction policy.
- Hardware board selection and official display/touch integration.

## Next recommended development step

Design Milestone 3B: authenticated pairing, key persistence, nonce allocation
and replay state before changing the event wire format.

## Last completed milestone

Milestone 3A: Cryptographic Foundation — portable Mbed TLS provider, desktop
known-answer and negative tests, and a passing ESP32-C6 hardware-test
application. This milestone establishes functionality, not production security
approval or encrypted message integration.
