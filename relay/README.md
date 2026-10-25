# Local relay

Run `python3 relay/falafel_relay.py --pair 1:2`; `--help` lists bind address,
port, data path, pair, and queue-limit options. It has no third-party dependency.

This is a development service. TCP is cleartext and identities are not
cryptographically authenticated, so bind only to trusted local interfaces. Its
JSON file contains hex events and deduplication keys and is persisted with
flush, `fsync`, and atomic replacement.
