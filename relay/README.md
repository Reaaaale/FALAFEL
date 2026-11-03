# Local relay

Run `python3 relay/falafel_relay.py --pair 1:2`; `--help` lists bind address,
port, data path, pair, and queue-limit options. It has no third-party dependency.

This is a development service. TCP is cleartext and identities are not
cryptographically authenticated, so bind only to trusted local interfaces.

The version-2 JSON state contains pending events and bounded completed-delivery
records. A new event is acknowledged only after a same-directory temporary file
has been flushed, `fsync`ed and atomically replaced; the directory is also
`fsync`ed where supported. Recipient socket writes never count as delivery.
Pending records are removed only after a valid device ACK is durably processed.

Defaults are 512 pending and 1,024 completed records. At the pending limit the
relay sends no acceptance and evicts nothing, allowing the sender to retry.
Clients have bounded output buffers and slow clients are disconnected without
removing their durable pending events. Corrupt or unsupported state prevents
startup instead of causing a silent reset.


## Encrypted mailbox transport v2 (3C.2C)

Use `--e2ee-only` with the headless mailbox peers or LVGL simulator to accept only `HELLO2`
registrations and envelope-carrying v2 data/receipt frames. This mode also rejects
existing legacy plaintext state. Default mode retains v1 compatibility. Numeric
registration remains unauthenticated; E2EE authentication is performed only at
the endpoints. No private keys are configured on the relay.

V2 caches exact ciphertext frames under source/128-bit ID/kind. Both data and
peer receipts survive restart and are redelivered on reconnect. A data retry also
recovers a cached reverse receipt. The relay generates no v2 delivery ACK and
cannot confirm authenticated receipt itself. Conflicting bytes for an existing
key are rejected. File and directory fsync are mandatory for new encrypted
records before forwarding. The JSON storage version remains 2, independently
of wire version; encrypted records add `version: 2` and a hex message ID.

Unlike legacy completed records, encrypted data/receipt entries are retained
without automatic eviction. Default capacity is 512 total pending entries;
configure enough space for data and receipts (up to 32 for legacy fixed eight-ID
mailboxes; 100 messages in each direction from upgraded simulators use 400 frames). Full means backpressure and continued sender
outbox retry, not loss or completion. Slow sockets still use the existing bounded
buffers and disconnect policy. Metadata is visible and active attackers can deny
service or fill/poison the cache; TLS/authenticated routing and safe compaction
remain future work. See `docs/protocol.md` and
`experiments/desktop-mailbox/README.md` for formats and test commands.

The LVGL simulator uses the existing v2 protocol without relay changes. See
`simulator/README.md` for explicit identity/mailbox setup and two-window tests.

The simulator's local archive/slot-reuse migration does not change relay frames.
Its eight active slots are reusable, so cached frame counts now grow beyond the
legacy workload. The default 512 cache entries and 65536-byte client output/replay
buffer are independent limits; neither relay GC nor paginated replay is added by
local history. Plan/test those limits before larger deployments. See
`docs/application-persistence.md` for retained-ID policy and the verified 100-per-
direction workload.
