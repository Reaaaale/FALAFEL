# ESP32-C6 E2EE lab runtime

## Status (2026-11-11)

The user confirms successful real bidirectional ESP32-C6 (ID 1) <-> relay in
Proxmox LXC 100 <-> Mac simulator (ID 2) E2EE. The relay reset/reconnect fix is
locally verified; remote deployment and physical RESET/EN acceptance are pending.
See [relay-only deployment and rollback](../relay/README.md#manual-deployment-to-proxmox-lxc-100).
The historical commissioning observations below describe the earlier session.
The new automated integration runs the actual C6 mailbox/stream sources on the
host with an NVS API model, the unchanged relay and real SDL/LVGL simulator.
It is not evidence of Wi-Fi, physical NVS or hardware entropy operation.

The lab worker owns Wi-Fi/TCP, a provisioned temporary identity and the existing
falafel_mailbox/falafel_mail_link/provider/NVS callbacks. It parses the unchanged
4-byte-prefix v2 DATA/RECEIPT frames, buffers partial I/O, retries exact persisted
ciphertext, and recovers saved receipts on reconnect. Only a verified E2EE receipt
and successful mailbox commit logs DELIVERED_AUTHENTICATED; TCP writes and HELLO2
never do. The new Message Manager is not used. Crypto diagnostic/SAR ownership,
E2EE formats are unchanged; relay registration now replaces stale same-ID sessions. No TLS/mTLS in this phase.

## Identity and persistence contract

- Use the C6 peer's own 64-byte identity (local private key + paired Mac public
  key), not a copy of the simulator's identity. Keys must already be reciprocal.
  If only the Mac identity survives, its paired public key cannot recover a lost
  peer private key. Do not rotate/overwrite Mac stores to repair that silently.
- No configured simulator was running during inspection; no identities were
  found in documented temporary/configuration paths. Actual existing identity,
  mailbox/history paths and routing IDs remain to be confirmed from its launch
  arguments. Locating files does not require printing/reading private contents.
- IDs 1 (C6), 2 (Mac) below are examples matching firmware defaults, not a verified
  assignment of the user's simulator. Reverse them if its existing own ID is 1.
- The importer reads a selected credential programmatically, never prints it,
  requires owner-only file permissions, refuses in-repository credential paths
  and existing output directories, and never modifies the source identity.
- It prepares a fresh **1 MiB falafel_data NVS partition image** with namespace
  falafel_id, blob identity: FCI1 + own/peer big-endian uint32 + credential64 +
  SHA256(previous76), 108 bytes total. This is trusted local lab provisioning,
  not authenticated enrollment or protected-key storage. The checksum is not a
  MAC. Private keys remain plaintext in this off-repository image and device NVS.
- The worker validates that record and configured IDs, derives its public key,
  and opens/explicitly creates the existing mailbox in namespace falafel. No
  namespace/key extension is made to the shared NVS backend or wire protocol.
- Exactly one preset is generated if the outbox has never been used. Pending and
  completed outbox records survive restart: reboot does not allocate a new ID or
  encrypt another preset. Changing the configured text does not replace it.
- This minimal mailbox deliberately keeps local format v1: **eight lifetime
  records per direction**, no history/archive migration or slot reclamation.
  It interoperates with the simulator's archived local-format-v2 mailbox because
  both use the same wire v2. FULL is explicit, never eviction/reset.
- Incoming DATA is authenticated and durably accepted by the mailbox; local
  consumption is persisted. Serial diagnostics contain IDs/states only, not
  plaintext or keys. No exactly-once external log/effect guarantee is claimed.

## Configure and build without real secrets in the repository

From the repository root, use the pinned IDF 5.5.6 environment:

```sh
FALAFEL_BUILD=/private/tmp/falafel-c6-e2ee-build
bash tools/with-idf-5.5.6.sh idf.py -C firmware -B "$FALAFEL_BUILD" \
  -DIDF_TARGET=esp32c6 -DSDKCONFIG="$FALAFEL_BUILD/sdkconfig" \
  -DSDKCONFIG_DEFAULTS="$PWD/firmware/sdkconfig.storage.defaults;$PWD/firmware/sdkconfig.c6-lab.defaults" menuconfig
```

In **Falafel LAN relay transport**, enter Wi-Fi SSID/password locally, the
reachable private relay IPv4/port, and matching own/peer IDs. Current build has
empty credentials/address and will refuse networking until configured. This
preparation supports WPA2-or-newer, 1..32-byte SSID, 8..63-byte password and private
IPv4 only. No hostname/IPv6/open Wi-Fi. Credentials live in the external build's
sdkconfig, generated headers and firmware image: keep these artifacts private.

The lab overlay explicitly permits initial mailbox creation and removes the
unused public CA bundle (no TLS). After initial creation you may disable
FALAFEL_LAB_CREATE_MAILBOX so missing mailbox state fails rather than creating it.
Existing corrupt/mismatched state is always an error, never automatically erased.
Wi-Fi credentials are RAM-configured, SDK nvs remains separate from falafel_data.
Crypto starts after Wi-Fi obtains IP; modem power save is disabled. No SAR source
from the RF-disabled diagnostic is enabled. Actual target entropy is unverified.

```sh
bash tools/with-idf-5.5.6.sh idf.py -C firmware -B "$FALAFEL_BUILD" build size
```

## Initial provisioning, flash and monitor (NOT executed)

First identify the C6's reciprocal identity and verify the Mac paths/IDs. If
using an already upgraded simulator store, keep its identity/mailbox/history
unchanged and do not use --init-history for the normal run. Never share one store
between simultaneous clients. Use a new disposable pair only with explicit user
agreement; the existing fixture provisioner can create one in a NEW external
private directory, but no new permanent pair was provisioned in this session.

```sh
FALAFEL_C6_IDENTITY=/ABS/PATH/TO/C6.identity
FALAFEL_IDENTITY_DIR=/private/tmp/falafel-c6-initial-provision
bash tools/with-idf-5.5.6.sh python tools/prepare-c6-identity.py \
  --identity "$FALAFEL_C6_IDENTITY" --own 1 --peer 2 \
  --output-dir "$FALAFEL_IDENTITY_DIR"
ls /dev/cu.*
FALAFEL_PORT=/dev/cu.YOUR_C6_PORT
```

**The following first command replaces the entire application data partition.**
Use only once for explicitly disposable initial provisioning. Do not repeat it
on reconnect, reboot or firmware update: that would erase IDs and deduplication
state. It does not write the Mac's files. No erase_flash/eFuse operation is needed.

```sh
bash tools/with-idf-5.5.6.sh python -m esptool --chip esp32c6 \
  --port "$FALAFEL_PORT" --after no_reset write_flash \
  0x100000 "$FALAFEL_IDENTITY_DIR/falafel-data.bin"
bash tools/with-idf-5.5.6.sh idf.py -C firmware -B "$FALAFEL_BUILD" \
  -p "$FALAFEL_PORT" flash monitor
# Subsequent observation without flashing:
bash tools/with-idf-5.5.6.sh idf.py -C firmware -B "$FALAFEL_BUILD" \
  -p "$FALAFEL_PORT" monitor
```

The current partition table is unchanged: standard nvs at 0x9000, app at 0x10000,
falafel_data at 0x100000, total configured flash 2 MiB. A normal app flash writes
bootloader/partition table/app, not the separately provisioned falafel_data image.

## Historical Proxmox LAN proposal (2026-11-08)

User-supplied deployment: host 192.168.1.200, unprivileged LXC 100 on vmbr1,
container 10.77.0.2/gateway 10.77.0.1; relay 10.77.0.2:8765, pair 1:2,
--e2ee-only, script /opt/falafel/relay/falafel_relay.py,
service falafel-relay.service, state /var/lib/falafel/state.json.
SSH username and deployed revision have not been confirmed. No SSH login attempted.

Read-only Mac probe: route to 10.77.0.2 uses default gateway 192.168.1.1, not
Proxmox; TCP 10.77.0.2:8765 timed out. TCP 192.168.1.200:8765 returned errno 65
(unreachable). Causes beyond that observation are not established.

Minimum proposed path, **requires user review/approval before installation**:
Mac/C6 -> 192.168.1.200:8765 (LAN-only host TCP proxy) -> 10.77.0.2:8765.
This leaves the Python relay, its listener and container state unchanged. It
requires no router WAN forwarding and no new L3 forwarding route on the clients.
Example units to review on Proxmox; verify the proxy executable path first:

`/etc/systemd/system/falafel-lan-proxy.socket`:

```ini
[Unit]
Description=Falafel LAN-only TCP listener
Wants=network-online.target
After=network-online.target
[Socket]
ListenStream=192.168.1.200:8765
NoDelay=true
[Install]
WantedBy=sockets.target
```

`/etc/systemd/system/falafel-lan-proxy.service`:

```ini
[Unit]
Description=Falafel LAN to isolated relay proxy
Requires=falafel-lan-proxy.socket
[Service]
ExecStart=/usr/lib/systemd/systemd-socket-proxyd 10.77.0.2:8765
DynamicUser=yes
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes
```

Firewall changes, if required by the ACTUAL configuration, should permit only
192.168.1.0/24 to this host TCP listener and host-to-container TCP 8765. Exact
rules cannot be safely supplied without reading the existing ruleset. No units,
firewall, bridge, forwarding, container or router configuration was modified.
If installed after review, first test the LAN listener from Mac, then configure
C6 and simulator to 192.168.1.200:8765. A successful TCP connect is not proof of
relay version compatibility or E2EE delivery. Keep the service off the Internet.

## Actual end-to-end acceptance sequence

Launch the EXISTING simulator with its verified paths, own/peer IDs and reachable
LAN relay host/port. Retain its existing identity and history; do not migrate or
reset them to run this test. Then attach/configure/flash the C6 as above.

Expected serial events: Wi-Fi obtained IPv4, E2EE STORED id=..., HELLO2 written,
then E2EE DELIVERED_AUTHENTICATED id=... for that SAME ID. On later boots expect
RESTORED, not a new STORED. The Mac must show the preset exactly once. Click its
existing Sto arrivando action: C6 logs RECEIVED and Mac shows authenticated delivery.
Interrupt/restart relay or Wi-Fi, then reboot C6 WITHOUT reprovisioning: there
must be no extra GUI row for the original ID and no new preset ID. Record serial
RESOURCES and actual UI result. The user has since confirmed the bidirectional
exchange; repeat RESET/EN after deploying the reconnect fix, without restarting
the relay between resets. Do not reprovision or erase persistent data.

## Verification performed

- Host normal and ASan/UBSan CTest: **19/19 each PASS**, including unchanged
  relay/transport/crypto/storage regressions and new c6_lab_mailbox test.
- c6_lab_mailbox: forged/duplicate receipt, genuine receipt plus injected failed
  NVS commit (no completion), exact saved ciphertext/reopen, incoming consumption,
  full inbox, wrong identity and no new boot ID. NVS is an API model, not flash.
- c6_simulator_e2ee: actual C6 lab/stream sources, 3-byte socket calls, interrupted
  upload/reconnect, authenticated receipt, NVS-model reopen, reverse preset and
  exactly one Mac received/delivered event; real local relay and SDL simulator.
  Only temporary test identities; user's simulator data was not touched.
- Provisioner: generated a private full-size NVS image from a temporary pair;
  checked FCI1/checksum, permissions, unchanged source and overwrite refusal.
- Fresh C6 IDF 5.5.6 build PASS: binary **902,000 B**, free app **81,040 B** of
  983,040 B. DIRAM statically used 165,370 B, unallocated 286,742 B. That is NOT
  measured runtime heap; Wi-Fi/crypto/task/NVS allocations reduce it. Firmware
  logs free/minimum heap, largest block and worker stack water mark every 30s.
- ELF symbols confirm the linked worker, credential loader, stream parser, lab
  mailbox initialization and envelope encryption, not only unused object compilation.
- No real identity imported, no serial session, no hardware flash/exchange and
  no Proxmox compatibility test in that earlier software-only session. The later
  physical exchange is user-confirmed; this fix is not yet deployed.

Reproduce host tests after building the existing simulator:

```sh
cmake -S . -B build/sec002/host \
  -DFALAFEL_C6_SIMULATOR_BUILD="$PWD/build/simulator"
cmake --build build/sec002/host -j4
ctest --test-dir build/sec002/host --output-on-failure
```

The optional simulator build path enables the integration CTest; it does not
build the simulator automatically. Reuse existing isolated Mbed TLS/Python and
sanitizer configuration when reproducing the recorded builds. Standalone driver:
`python3 test/test_c6_simulator.py HOST_BUILD/c6_lab_probe SIM_BUILD/falafel_simulator SIM_BUILD/falafel_mailbox_fixture`.
