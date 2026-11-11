# Falafel desktop transport protocols

The retained legacy test event protocol is version 1 and uses a four-byte network-order TCP
frame length followed by one serialized event. Its fixed event header is:

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | Magic `FL` |
| 2 | 1 | Version (`1`) |
| 3 | 1 | Kind: text `1`, personality `2`, ACK `3` |
| 4 | 4 | Source ID |
| 8 | 4 | Destination ID |
| 12 | 8 | Event ID |
| 20 | 8 | Timestamp |
| 28 | 8 | Personality logical revision |
| 36 | 1 | Personality value |
| 37 | 1 | ACK phase |
| 38 | 2 | Payload length |

All integers are big-endian. Payload follows the 40-byte header and is limited
to 256 bytes. `RELAY_ACCEPTED` is ACK phase 1 and `DEVICE_RECEIVED` is phase 2.

The relay status form of `DEVICE_RECEIVED` has source `FALAFEL_RELAY_ID` and
means that a valid peer ACK was previously durably recorded. It is status
recovery, not cryptographic proof from the peer.

Retries reuse the exact event ID. The relay routes only its configured pair and
matches a device ACK to the reverse source/destination and original event ID.
Malformed, oversized, unknown-version and unknown-kind frames close the
connection.

This legacy protocol is not production secure. The E2EE mailbox uses the
separate v2 path below; numeric v1 event IDs and ACKs are never treated as
authentication or used as an encrypted-simulator fallback.

Milestones 3A, 3B.1, 3B.2 and 3B.3 do not alter this format. The cryptographic
provider and pairing harness are tested in isolation only; no event-v1 field is
encrypted or cryptographically authenticated. Nonce allocation, replay
protection and incompatible-version handling must be defined with the future
E2E envelope before runtime use.

Milestone 3C.2A implements an isolated offline Noise K envelope, described in
`async-e2ee-design.md`. Its maximum is 256 bytes including 134 bytes of overhead
(122-byte plaintext limit). It is not assigned an application event type or
connected to this transport. Existing ACKs remain unauthenticated.

Milestone 3C.2B adds an isolated mailbox receipt protocol inside envelope v1:
`FMSG` + version byte 1 + up to 117 application bytes, or `FACK` + version byte 1
+ status byte 1 (durable inbox acceptance) + SHA256(original data envelope).
An ACK is a reverse-direction Noise K envelope (172 bytes) with the original
16-byte message ID. No legacy event-v1 ACK phase is cryptographic proof, and
none is passed to the mailbox as confirmation. See `async-e2ee-design.md` for
persistent recovery, field bindings and the explicit application consumption
contract. The v1 event kinds remain unchanged. 3C.2C supplies the separate v2 framing below.


## Encrypted mailbox transport v2 (3C.2C)

A v2 connection registers with ASCII `HELLO2 <own> <peer>\n`. Legacy connections
continue to use `HELLO`; versions cannot be mixed on one connection. Run the
relay with `--e2ee-only` to reject all legacy clients and preexisting plaintext
state. Without that option it supports both for legacy test compatibility.
The LVGL simulator now uses v2 exclusively, with no plaintext fallback. Its
command/event queues are local application interfaces, not new wire messages.
Registration is routing, not cryptographic identity authentication.

The optional C6 lab worker also uses this unchanged v2 transport. Its local
mailbox format v1 (eight lifetime IDs/direction) interoperates with the simulator's
archive-backed local format v2; local versions are not negotiated on the wire.
The user confirms physical bidirectional C6/Proxmox/Mac E2EE; acceptance of the
relay reconnect fix is pending. See `c6-relay-transport.md`.

A valid registration for an occupied device ID replaces and closes its previous
TCP connection only when peer and transport version match. This recovers reset
clients even without FIN/RST; replay starts with complete frames from the same
cache. Invalid registrations and version mismatches leave the old owner intact.
This is unauthenticated routing policy, not a change to E2EE or wire formats.

The same four-byte big-endian TCP frame-length prefix is reused. A v2 frame is:

| Offset | Bytes | Field |
|---|---:|---|
| 0 | 2 | `FL` magic |
| 2 | 1 | transport version 2 |
| 3 | 1 | kind 4 = encrypted data; kind 5 = encrypted receipt |
| 4 | 4 | numeric source |
| 8 | 4 | numeric destination |
| 12 | 16 | opaque mailbox message ID |
| 28 | 2 | envelope length |
| 30 | 134..256 | unchanged envelope v1 |

Integers are big-endian. The codec requires data envelope lengths 139..256,
receipt length 172, exact envelope/header lengths and identical inner/outer IDs.
Data frames are 169..286 bytes, receipt frames 202; add four bytes on TCP.
Unknown versions/kinds, mismatched lengths/IDs and malformed envelope framing
are rejected. The relay validates shape/routing only; endpoints verify Noise
authentication and the encrypted data/receipt domain through `falafel_mailbox`.
Altering unauthenticated outer metadata cannot produce an authenticated receipt
or change the inner paired identities. No plaintext payload kind exists in v2.

There are **no relay-generated v2 ACKs**. The relay atomically caches the exact
frame under `(source, 128-bit ID, kind)` and routes it. Exact retries are
idempotent; conflicting ciphertext under the same key is rejected. Saved data
and receipts are retained through restart and redelivered on registration.
Retrying data also triggers delivery of any saved reverse receipt to its sender.
The relay never marks an endpoint outbox complete: only `falafel_mail_ack` can.
No read receipt or third layer of delivery ACK is added.

V2 cache entries share the existing bounded pending array, but never enter the
legacy completed-status array. They are retained, with no automatic eviction,
because the relay cannot authenticate endpoint completion. A full relay declines
new cache entries without erasing any: endpoint outboxes retry after capacity
is restored. Configure enough room for both data and receipts (default 512;
this fixed pair can generate up to 32 retained frames). New cache entries are
forwarded only after file and parent-directory fsync/atomic replacement succeed.
The version-2 JSON *storage* schema is independent of transport version, and v1
pending/completed records remain loadable in legacy/mixed mode.

Cached delivery is incremental within the 65,536-byte per-client output buffer.
The relay keeps ordered pending-record keys per connection, coalesces requests
for keys still waiting, and buffers a complete frame only when it fits. Partial
socket writes resume buffering; reconnect reconstructs replay from durable state.
Legacy records removed by ACK before buffering are skipped. This changes no wire
format, receipt semantics, persistent state or cache retention/GC policy.

The desktop adapter reuses its bounded buffers, asynchronous DNS, partial
read/write handling and reconnect loop. A queued frame is volatile; all durable
recovery comes from the mailbox/relay cache. Connection/handshake and stalled
partial-frame/write progress deadlines are five seconds, reconnect delay one
second. The portable coordinator schedules pending exact ciphertext retries at
one-second intervals, handles output-busy backpressure, and reschedules saved
receipts after reconnect or duplicate data. Receipt scheduling is fair with data;
no relay acceptance suppresses data retry. FULL and storage errors never advance
outbox completion; a poisoned mailbox stops the peer instead of resetting state.


## Local persistence versions (not network versions)

The persistent GUI uses the unchanged v2 DATA/RECEIPT frames above. Its explicit
local mailbox migration FMBX v1 -> v2 adds a 16-byte archive UUID (snapshot
8376 -> 8392 bytes, records shift from offset 72 to 88). That UUID must match
the authenticated encrypted application archive before operations/slot release.
The existing 517-byte record encoding and SHA256 checksum remain; no new wire
field, ACK kind, read receipt, epoch or registration message is introduced.
Retired duplicate DATA returns its exact archived authenticated receipt; late
ACKs are checked against archived ciphertext. Full/corrupt/missing storage never
permits ID eviction or fallback. See `application-persistence.md` for migration,
compatibility, safe consumption and retention limits.
