# Falafel desktop event protocol

The current wire protocol is version 1 and uses a four-byte network-order TCP
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

This protocol is not production secure. A future E2E envelope must sit between
the application codec and transport without treating these numeric IDs as
authentication.
