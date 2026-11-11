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
Clients have bounded output buffers. Cached deliveries wait for buffer space
instead of disconnecting slow readers; socket errors still close the connection
without removing durable pending events. Transient legacy ACK output retains
its existing overflow/disconnect policy. Corrupt or unsupported state prevents
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
outbox retry, not loss or completion. Slow sockets use incremental cached delivery
within the existing bounded output buffer. Metadata is visible and active attackers can deny
service or fill/poison the cache; TLS/authenticated routing and safe compaction
remain future work. See `docs/protocol.md` and
`experiments/desktop-mailbox/README.md` for formats and test commands.

The LVGL simulator uses the existing v2 protocol without relay changes. See
`simulator/README.md` for explicit identity/mailbox setup and two-window tests.

The C6 lab reuses those same DATA/RECEIPT frames. The user confirms successful
real bidirectional C6/Proxmox/Mac E2EE. The reset/reconnect fix below is locally
verified; its deployment and physical reset acceptance remain pending.
See `docs/c6-relay-transport.md` for the lab procedure and proposed LAN-only
host proxy. No proxy, firewall, router or deployed relay changes were applied.

The simulator's local archive/slot-reuse migration does not change relay frames.
Its eight active slots are reusable, so cached frame counts now grow beyond the
legacy workload. The default 512 cache entries and 65536-byte client output
buffer are independent limits. Since 2026-11-03, replay is incremental: each client
keeps ordered, coalesced pending-record keys, and frames enter the output buffer
only when they fit. Successful partial writes resume this work. Live cached
deliveries use the same path; repeated requests for a still-waiting key coalesce.
Legacy records retired before buffering are skipped. Reconnect rebuilds delivery
from durable cache, including any frame interrupted mid-write. No disk format,
frame, ACK or cache-retention policy changes. Relay GC remains unimplemented. See
`docs/application-persistence.md` for retained-ID policy and the verified 100-per-
direction workload.

## Relay regression tests

Run `PYTHONDONTWRITEBYTECODE=1 python3 -X dev test/test_relay.py` from the repository
root (Python standard library and local sockets only). On 2026-11-03 all 10 tests
passed, including the six existing cases. Two new 226-maximum-DATA regressions
failed on the original relay with a disconnected recipient, then passed after
the fix. Coverage includes a slow real TCP reader after relay restart, 37-byte
partial writes, would-block/interrupted writes, a broken pipe mid-frame and
reconnect, 512 cached/live frames with coalesced waiting retries, legacy ACK
retirement and removal of write interest after draining. Opaque v2 fixtures test
relay routing/framing; they do not test endpoint cryptography or provision keys.


## Same-ID reconnect after device reset

A hardware reset may leave the old TCP session established: no FIN/RST reaches
the relay. Previously `own in by_device` rejected every new HELLO2 until that
old session disappeared (or the service restarted). A fully validated allowed
registration now closes/unregisters the previous connection for that ID when
peer and version match, then installs the new connection and rebuilds cached
replay. Only one connection owns the ID; the latest valid registration wins,
even if the previous TCP still looks live. Invalid or mismatched registrations
cannot evict it. Existing identity-checked cleanup and event-loop membership
checks prevent old cleanup/events from removing the replacement connection.

No E2EE, identity, JSON-state, cache-retention or ACK changes. Incomplete output
belongs to the old stream; the new stream starts with complete cached frames.
Registration remains unauthenticated; same-ID simultaneous clients can displace
each other. This is still a trusted-LAN development relay.

Firmware review: `falafel_relay_tcp_open()` closes descriptors on all failures;
`network_worker()` closes on Wi-Fi loss, stream error and task exit before
reconnecting. Each successful open reinitializes stream buffers. RESET/EN cannot
reliably send a TCP close, so the recovery belongs in the relay. Firmware needs
no patch or flash for this correction.

Verification on 2026-11-11: host builds pass; all 19 CTest entries pass in both
normal and ASan/UBSan configurations after correcting test synchronization.
The 15-case relay suite passes five consecutive times per configuration. Five
new tests cover lost FIN/RST with three same-ID reconnects and unchanged disk
state, FIN and TCP RST cleanup, replay after partial old output, invalid
registration and mixed-version rejection. The lost-FIN regression failed on the
original implementation. The existing C6 adapter test now verifies replacement
against a running relay subprocess. Sanitizers apply to C code, not Python.
No physical reset test or remote deployment was performed in this session.

## Manual deployment to Proxmox LXC 100

Only runtime file to update: `relay/falafel_relay.py` ->
`/opt/falafel/relay/falafel_relay.py` inside LXC 100. Tests/docs stay in the
repository. No firmware, service configuration, identities or state-file update.
These commands are supplied for review; they have NOT been executed.

First inspect the actual unit on the Proxmox host (read-only):

```sh
pct exec 100 -- systemctl show falafel-relay -p ExecStart -p User -p WorkingDirectory
pct exec 100 -- systemctl cat falafel-relay
```

Confirm ExecStart uses the path above and retains the existing `--data` path
(documented deployment: `/var/lib/falafel/state.json`) and `--e2ee-only` flag.
If the deployed script path differs, adjust the commands accordingly.

From the Mac repository root, replace `YOUR_PROXMOX_HOST` with its SSH host:

```sh
scp relay/falafel_relay.py root@YOUR_PROXMOX_HOST:/tmp/falafel_relay-reconnect.py
```

Then on the Proxmox host, back up the current script once, stage and validate the
replacement, preserve the installed owner/mode and atomically replace it:

```sh
set -e
pct push 100 /tmp/falafel_relay-reconnect.py /tmp/falafel_relay-reconnect.py
pct exec 100 -- python3 -c 'import ast; ast.parse(open("/tmp/falafel_relay-reconnect.py").read())'
pct exec 100 -- sh -eu -c '
  target=/opt/falafel/relay/falafel_relay.py
  test ! -e "$target.pre-reconnect"
  cp -p "$target" "$target.pre-reconnect"
  cp -p "$target" "$target.next"
  cat /tmp/falafel_relay-reconnect.py > "$target.next"
  mv "$target.next" "$target"
'
pct exec 100 -- systemctl restart falafel-relay
pct exec 100 -- systemctl is-active falafel-relay
pct exec 100 -- journalctl -u falafel-relay -n 50 --no-pager
```

One service restart loads the patch. Then press C6 RESET/EN repeatedly WITHOUT
restarting the relay again: expect RESTORED, stable HELLO2 registration and
bidirectional authenticated exchange, with existing IDs and no duplicate GUI
rows. Do not reinitialize mailbox/history or replace the state JSON.

Rollback on the Proxmox host, if required:

```sh
pct exec 100 -- cp -p /opt/falafel/relay/falafel_relay.py.pre-reconnect /opt/falafel/relay/falafel_relay.py
pct exec 100 -- systemctl restart falafel-relay
```
