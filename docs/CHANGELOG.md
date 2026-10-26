# Falafel development changelog

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
