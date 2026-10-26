# Falafel architecture

```text
LVGL GUI -> portable C11 application -> event protocol
         -> non-blocking desktop adapter -> durable local relay
         -> peer adapter -> application -> LVGL GUI
```

The portable code under `shared/app` and `shared/include` has no LVGL, SDL,
POSIX, networking, filesystem, or ESP-IDF dependency. The desktop adapter owns
DNS, sockets and local files. The relay owns routing and durable delivery state.
A future ESP-IDF adapter can provide equivalent transport and persistence
services without changing the application state machine.

## Protocol roles

`falafel_message_t` is the original 19-byte legacy packet and remains available
only for compatibility and its existing tests. The desktop MVP uses
`falafel_event_t`, a version-1 routed event with a deterministic 40-byte
big-endian header and at most 256 payload bytes.

Event kinds are text, personality update and acknowledgement. Deserialization
rejects unknown versions, kinds, lengths and malformed packets before modifying
the destination. The application independently enforces origin and destination:

- text and personality events must come from the configured peer;
- `RELAY_ACCEPTED` must come from `FALAFEL_RELAY_ID`;
- `DEVICE_RECEIVED` must come from the peer or from the relay's persisted final
  status;
- every destination must be the local device.

IDs are authorization hints only. The protocol is cleartext and has no
cryptographic authentication.

## Reliability state machine

```text
local creation
    |
    v
QUEUED -- durable relay commit --> RELAY_ACCEPTED
    ^                                  |
    | retry same event ID              | retry/status recovery
    +----------------------------------+
                                       |
                          durable recipient record
                                       |
                                       v
                              DEVICE_RECEIVED
```

`RELAY_ACCEPTED` means the event is present in the relay's atomically replaced
state file. A socket write to the recipient does not advance this state.
`DEVICE_RECEIVED` is generated after the receiving simulator has committed its
application state. The relay then atomically moves the event from `pending` to
`completed`. If the original sender is offline, retransmitting the same event
after reconnecting recovers both statuses from the relay.

The sender retries both `QUEUED` and `RELAY_ACCEPTED` records. Duplicate ACKs
are idempotent. An ACK can affect only a matching locally queued ID.

## Relay persistence and backpressure

The relay file is JSON format version 2 with validated `pending` and `completed`
records. New events are persisted before acceptance is queued. Recipient ACKs
are persisted before a pending record is removed or final status is forwarded.
Writes use a file in the same directory, `flush`, `fsync`, atomic replacement,
and directory `fsync` where supported.

Default bounds are 512 pending and 1,024 completed records. A full pending queue
does not evict data and does not acknowledge the new event; the sender retains
and retries it. Completed retention is bounded: after 1,024 newer completions,
relay-level historical deduplication is no longer guaranteed.

## Device persistence and event IDs

Desktop state format v2 uses explicit big-endian fixed-width fields, length
checks, event serialization and CRC-32. It contains:

- personality value, revision and author;
- outgoing queue and delivery states;
- 64 recent received identities;
- up to 32 durably received text events;
- the reserved event-ID high-watermark.

State writes use a temporary file, `flush`, `fsync` and atomic rename. Corrupt
and unsupported formats stop startup; they are never silently reset. MVP format
v1 is intentionally rejected because it cannot prove the next safe ID after all
outgoing events were removed.

IDs use the device ID in the high 32 bits and a persistent sequence in the low
32 bits. The adapter durably reserves blocks of 64 values before allocation.
After a crash or restart, unused values are skipped. Exhaustion fails explicitly.

## Deduplication and personality convergence

The application persists 64 recent `(source, ID)` identities. Text duplicates
inside that bounded window do not produce duplicate application deliveries.
The relay also persists pending events and 1,024 completed identities. These are
bounded policies, not unlimited replay protection.

Local personality events are coalesced only before their first successful
transport submission. Submitted or relay-accepted events are retained until
terminal confirmation. The relay never discards older accepted personality
events. Remote updates use the greatest `(revision,
author_device_id)`, making duplicates and older events harmless and simultaneous
updates deterministic without retransmission loops.

## Desktop transport

DNS resolution runs on a short-lived worker thread. Socket creation, connect
completion, handshake, reads and writes are non-blocking. Connect completion is
checked with zero-timeout readiness polling and has a five-second timeout;
reconnect attempts are spaced by one second. Input and output buffers are
bounded. Partial I/O, `EINTR`, `EAGAIN`, resets and remote closure are handled.

The relay uses selector-managed output buffers rather than `sendall()`. Slow
clients cannot block the event loop and are disconnected when their 64 KiB
output limit is exceeded; durable pending records remain available.

## Security and future hardware

Milestone 3A adds `shared/include/falafel_crypto.h` and an Mbed TLS provider in
`shared/crypto/`. The API exposes no Mbed TLS types and is independent of LVGL,
SDL, TCP and ESP-IDF. A caller-owned opaque context contains entropy and CTR-DRBG
state; X25519, HKDF-SHA256 and AES-256-GCM are available on desktop and ESP-IDF.
AES-256-GCM was selected because both C6 and S3 expose AES acceleration and
ESP-IDF enables GCM by default. No insecure fallback exists.
The only new dependency is Apache-2.0-licensed Mbed TLS: desktop builds pin
3.6.7, while ESP-IDF supplies its managed copy. The opaque RNG context reserves
1,536 bytes; temporary key-operation memory is owned by Mbed TLS. C6 and S3 use
the same provider source, with hardware acceleration selected by ESP-IDF.

The provider does not manage nonce uniqueness, pairing, key persistence or
replay windows. Those remain responsibilities of a future versioned E2E
envelope. Event wire v1 and the runtime application path are unchanged.

TCP registration and traffic remain unauthenticated and unencrypted. Source-ID
checks prevent accidental cross-routing but not a malicious peer or relay. The
relay sees all content and metadata. End-to-end encryption, TLS, production
identity, ESP32-S3 NVS, Secure Boot and flash encryption remain future work.
