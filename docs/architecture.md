# Falafel architecture

```text
LVGL GUI -> portable application -> versioned event protocol
         -> POSIX desktop adapter -> local TCP relay -> peer adapter
         -> portable application -> LVGL GUI
```

`shared/app` and `shared/include` are C11, fixed-capacity, and contain no LVGL,
SDL, POSIX, ESP-IDF, or dynamic allocation. The simulator owns sockets and local
state files; the relay owns routing and offline persistence. A future ESP-IDF
adapter can implement the same transport/persistence boundaries with Wi-Fi/NVS.

## Protocol and delivery

Legacy `falafel_message_*` packets remain supported. The MVP envelope starts
with magic `FL`, version 1, then deterministic big-endian kind, source,
destination, event ID, timestamp, logical revision, personality, ACK phase,
payload length and payload. Its header is 40 bytes; payload is at most 256 bytes.

```text
CREATED -> QUEUED -> RELAY_ACCEPTED -> DEVICE_RECEIVED -> READ (reserved)
```

Retry preserves the event ID and the relay deduplicates `(source, ID, kind)`.
Relay acceptance and device receipt are distinct ACKs. READ is not implemented.

## Personality convergence

A local change increments a logical revision. Remote application never invokes
the local-change callback, preventing loops. The greatest `(revision,
author_device_id)` wins, so simultaneous changes converge without physical-clock
agreement. Duplicates and older events are ignored. The relay retains the latest
undelivered personality update for each destination.

## Persistence, limits and transport

The relay atomically replaces validated JSON with at most 512 pending events by
default and 1,024 deduplication keys. Each desktop device atomically replaces a
versioned binary adapter-private file holding personality, revision, author, the
16-entry outgoing queue and 32 recent received identities. Missing/corrupt files
are ignored. TCP registration is `HELLO own peer`, followed by 32-bit
length-prefixed frames. The adapter polls non-blockingly and reconnects each
second. The relay permits only its configured pair and does not log bodies.

## Security and future hardware

Local TCP is deliberately cleartext and unauthenticated: it is not production
secure. Encryption belongs around serialized bytes outside the core; no custom
cryptography exists here. Display, touch, power, camera, provisioning, secure
identity, network transport and ESP-IDF persistence remain future work after an
ESP32-S3 board is selected from official documentation.
