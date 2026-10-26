# Falafel project status

Last updated: 2026-10-26

## Current status

The desktop reliability-hardening milestone is implemented and locally
verified. Falafel is a reliable development MVP for two simulator instances,
not production firmware and not a secure Internet messaging system.

## Implemented features

- Portable C11 application state and bounded queues.
- LVGL/SDL desktop simulator with messages and three personalities.
- Non-blocking TCP client including asynchronous DNS resolution.
- Durable Python relay with offline delivery and final-status recovery.
- At-least-once transport delivery with bounded application deduplication.
- Persistent non-reused event-ID ranges.
- Versioned, checksummed desktop state.
- Deterministic personality synchronization and safe unsent coalescing.

## Verified functionality

- Core and protocol unit tests under ASan/UBSan.
- Relay persistence failure, restart, backpressure and fragmented I/O tests.
- Desktop application/transport/relay round trip with no duplicate application
  delivery.
- Corrupt and unsupported desktop and relay state rejection.
- Desktop simulator compilation on macOS.

CI is configured for GCC, Clang, sanitizer tests and an Ubuntu simulator build.
The updated CI workflow has not yet run remotely in this working tree.

## Current architecture

See `docs/architecture.md`. Application events pass through the portable core,
versioned protocol, desktop transport and durable relay. Platform persistence
and networking remain outside the shared core.

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

## Pending tasks

- End-to-end security architecture and implementation.
- Production relay authentication and transport security.
- ESP-IDF networking and NVS persistence adapters.
- Conversation retention/compaction policy.
- Hardware board selection and official display/touch integration.

## Next recommended development step

Begin the approved E2E cryptographic provider increment, keeping encrypted
session state behind the same persistence boundary.

## Last completed milestone

Milestone 2: Reliability Hardening — durable ACK lifecycle, crash-safe ID
allocation, bounded deduplication, non-blocking networking, portable persistence
format, fault-injection coverage and end-to-end desktop transport test.
