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
