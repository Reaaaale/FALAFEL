# Application history and safe mailbox retirement

Implemented and verified on desktop, 2026-10-31. This changes **local persistence**,
not the Noise K envelopes, HELLO2 registration or v2 DATA/RECEIPT wire protocol.

2026-11-01: an ESP-IDF NVS adapter now implements the same callbacks without
changing these blob formats. C6 cross-build and normal/sanitized host recovery
tests pass; physical power-cut/GC, actual capacity and S3 verification remain
pending. There is no firmware consumer or identity provisioning yet. The embedded
crypto issue still excludes real identities/data. See [embedded storage](embedded-storage.md)
for backend selection, atomicity assumptions and physical test checklist.
The E2EE provider and relay implementation are reused unchanged.

## Ownership and storage

The messaging pthread owns the existing crypto context, mailbox/link/TCP and the
new portable `falafel_history` store. LVGL still communicates through bounded
8-command/32-event queues and processes at most four events per loop. No storage,
crypto or retirement work runs in the LVGL thread.

Each history message contains its opaque 128-bit ID, source/destination numeric
IDs and public keys, insertion sequence, text/length, direction, delivery state,
local read flag and the original mailbox envelope/receipt. IDs are scoped by
paired identity and direction, equivalently sender public key + message ID.
Sender/recipient/text/ID are immutable; delivery, local read and mailbox state
only advance. Repeating an update is a no-op. A reused ID with different content
is a conflict, never a second conversation row.

The generic storage interface loads/replaces named blobs and scans committed
record names. The POSIX adapter reuses the existing atomic-file implementation:
0600 temporary files, full write, file fsync, rename, directory fsync. The namespace
is a 0700 owned directory with one exclusive lock. Its parent is fsynced on open,
including reopening after an interrupted directory creation. Temporary crash
files contain ciphertext, are ignored, and may occupy extra space until explicit
maintenance; committed records are never silently discarded.

The encrypted manifest lists every committed ID/direction. Every indexed record
is checked at open; missing, corrupt, wrong-key or swapped records fail closed.
New records are written before manifest publication. After an interrupted new
record transaction, an authenticated orphan with the next insertion sequence is
adopted into the manifest. Write failure poisons the handle: reopen to resolve
whether the atomic replacement committed; do not continue or reset it.

## Local encryption and keys

Local blobs use existing provider **AES-256-GCM**, with fresh 96-bit CSPRNG nonces
for every write and 128-bit tags. No new cryptographic dependency or network
cipher is introduced. Nonce uniqueness is probabilistic, relying on the existing
provider's properly seeded RNG; no counter resets to zero on reboot.

The 32-byte key is HKDF-SHA256 with:

- IKM: the exact 32 local private-key bytes already loaded by the runtime;
- salt: local public key followed by paired public key;
- info: ASCII `Falafel local history AES256GCM v1`.

This domain is separate from network keys and does not use the DH shared secret:
the peer cannot derive the local archive key from its own pairing key. AAD is the
40-byte blob header followed by the local/peer public keys and 16-byte archive
UUID (120 AAD bytes total). Magic, direction, ID and nonce are authenticated; swapping files or using another pair fails.
The worker erases the history context/derived key at shutdown and clears normal
decoded-record/history scratch buffers after use; this is not a promise against
OS swap, screenshots or forensic recovery of every transient stack copy.

There is no plaintext text/history file or raw local AES-key file. This is **not**
protection against an attacker who can read the simulator's 0600 fixture identity:
that file contains the private key and permits derivation of the archive key.
Production Keychain/secure ESP32-S3 key storage is not yet wired. Back up identity,
mailbox and history together while the worker is stopped; loss/change of the
private key or paired public key makes old history unavailable. Key rotation,
peer replacement and export require an explicit future decrypt/re-encrypt
migration, never deleting the old key first. File names expose IDs/direction;
manifest length exposes count. Corruption is detected, not repaired. Authenticated
old-state rollback or coordinated deletion/replacement by a local attacker is not
prevented; neither the mailbox checksum nor GCM supplies a trusted monotonic counter.

## Blob formats

A record is exactly **800 bytes**: 40-byte header, 744-byte ciphertext, 16-byte tag.
The header has `FHR1` at offset 0, nonce at 8, direction at 20 and ID at 21;
remaining header bytes are zero. The decrypted body is:

| Offset | Bytes | Content |
|---|---:|---|
| 0 | 6 | direction, delivery, read, text-missing flag, text length, mailbox state |
| 6 | 4 | envelope length and receipt length (16-bit big-endian) |
| 10 | 6 | reserved zero |
| 16 | 16 | message ID |
| 32 | 8 | insertion sequence (big-endian) |
| 40 | 8 | source and destination numeric IDs (32-bit big-endian) |
| 48 | 64 | sender and recipient public keys |
| 112 | 117 | text, zero padded |
| 229 | 256 | original envelope |
| 485 | 256 | original receipt |
| 741 | 3 | reserved zero |

The manifest uses `FHI1`, direction 255, its random 128-bit archive UUID in the ID field, the same nonce/AAD/tag layout,
and a body of 32-bit count followed by 17-byte direction/ID entries. Size is
**60 + 17 × count** bytes. Insertion sequence is the manifest position + 1;
records must agree. Formats use explicit fields, not native structure layout.

Delivery values are pending intent (0), durable active outbox (1), authenticated
acceptance (2). Incoming delivery 2 means local inbox accepted it. Read means
**read on this device**, not remote reading: no read-receipt protocol was added.

## Commit ordering and restart

```text
send request (volatile)
  -> history intent + manifest durable (STORED)
  -> mailbox enqueue durable -> history binds exact ciphertext (QUEUED)
  -> existing link/relay retry
  -> authenticated peer ACK -> mailbox completed durable
  -> history ACCEPTED + original ciphertext durable -> release active outbox slot

authenticated receive -> durable inbox + saved ACK
  -> durable idempotent history effect
  -> consume inbox
  -> history retains consumed envelope + exact saved ACK
  -> release active inbox slot
```

A full active outbox leaves the application intent pending on disk, not in an
unbounded RAM queue. On restart, an intent without ciphertext can be enqueued;
if an active slot already exists, recovery binds/retries those exact bytes and
never re-encrypts the ID. A crash after peer ACK but before history update is
recovered from the completed active slot. Slot release happens last.

A crash before incoming history commit leaves the available mailbox record.
After history commit but before consume, the same ID updates idempotently.
After consume but before release, the saved record/receipt is retained again
idempotently. After release, archive lookup authenticates exact duplicates and
returns the **same** saved receipt. Conflicting ciphertext under that ID is rejected.
Late valid ACKs are checked against archived ciphertext and return DUPLICATE.

Opening messages queues a local read update. Reads are persisted before the GUI
receives their update and are monotonic. GUI rows upsert by direction/ID, so
restoration, read changes, ACKs and relay replay do not append duplicate rows.
Texts, outgoing/incoming states and local reads all restore after restart; the
character, palette, positions, controls and 24-frame rotation are unchanged.

## Capacity and local migration

The active mailbox remains **eight records per direction**. `falafel_mail_reclaim`
only releases state-2 records after an archive callback durably retains them.
Replay IDs are not discarded, approximated or aged out. The coordinator also
queues exact archived ACKs in a bounded fair queue; if that volatile queue fills,
retry can recover the saved receipt again. No accepted pending record is evicted.

History currently has a fixed **1024 total IDs per paired device**, shared by
both directions, with explicit FULL and no pruning. This solves the eight-ID
lifetime limit by recycling active slots; it is not unlimited storage. Increasing
capacity or eventually compacting replay tombstones requires a reviewed memory,
retention and rollback policy. At history FULL, existing records and updates stay
valid; available inbox records are not consumed without an effect. If all eight
inbox slots fill, additional data gets FULL and no ACK. Persistent send intents
already accepted continue through the active window.

A v1 mailbox is upgraded explicitly with `--init-history` (and optional
`--history DIR`, default `<mailbox>.history`). First initialize/validate the
archive and import existing records, then commit `FMBX` version **2** bound to
that archive UUID, then enable slot release. The snapshot grows from **8376 to 8392 bytes**: the archive UUID
occupies offsets 72..87, outbox starts at 88, inbox at 4224 and checksum at 8360.
The 517-byte record encoding and hash algorithm are unchanged. Tests interrupt
this migration at each atomic-write stage. A retry with a valid initialized archive resumes it.

Version 2 records an archive dependency: core message operations reject an opened
v2 handle until matching retention is attached. A different valid archive of
the same key pair is rejected by UUID, and record tags also bind their namespace.
The simulator cannot create a missing archive for v2, **even with --init-history**. Old binaries cannot read v2 snapshots;
the headless fixture's run mode is not a retention-aware consumer. Downgrade is
not supported. Keep a coherent backup before upgrading.

Legacy inbound text can be recovered from retained encrypted envelopes. Legacy
outgoing text was never persisted and cannot be reconstructed from sender-side
ciphertext; migration retains IDs, envelope, status and receipt processing, and
shows `[Testo precedente non disponibile]` instead of inventing or dropping text.
All newly accepted application intents persist their actual text before enqueue.
A volatile command not yet confirmed STORED can still be lost on abrupt kill;
the crash guarantee starts at the durable transaction boundary, under the POSIX
fsync/atomic-replacement and intact-key assumptions, not arbitrary media failure.

The relay has independent limits: default 512 cached frames, with no
GC; 100 messages in each direction produce 400 DATA/RECEIPT frames. Its per-client
output buffer is 65536 bytes. Cached replay now waits incrementally for output
space instead of requiring the whole backlog to fit. Larger workloads still need
cache-capacity planning; no unlimited relay availability or network epoch protocol
is claimed. See `../relay/README.md` for the relay-only regression coverage.

## Verified resource use and tests

On the macOS builds used here:

| Resource | Bytes |
|---|---:|
| Portable history context (bounded metadata + two scratch buffers) | 55,520 |
| Controller + history, excluding provider allocations/pthread stacks | 93,376 |
| GUI ID/row-pointer cache (1024 entries, outside LVGL heap) | 49,152 |
| Temporary native message structure | 736 |
| Record on disk | 800 |
| Manifest at 200 messages / maximum 1024 | 3,460 / 17,468 |
| History at 200 messages / maximum 1024 | 163,460 / 836,668 |
| Upgraded mailbox snapshot | 8,392 |
| LVGL observed peak with 200 rows (without snapshots) | 3,067,008 |

Filesystem allocation, lock files, temporary crash files, executable/PNG assets,
OS/SDL, allocator overhead and sanitizer runtime are additional. The existing
LVGL heap budget is 8 MiB; snapshot smoke peak was about 3.75 MiB. No ESP32-S3 RAM,
stack, flash endurance or power-cut measurements are claimed.

Normal and ASan/UBSan simulator CTests passed **4/4**: rotation smoke; encrypted
history (100 messages each direction, 41 real process-death boundaries, full
active mailbox/full history, exact archived replay, read state, wrong-key and
corruption); controller (five cases including legacy migration, missing archives,
wrong-archive UUID rejection and runtime storage failure); real SDL integration
(three cases including 100
messages each direction, 200 restored rows/device, local reads/delivery states,
relay offline/restart, duplicates, window close). Relevant history/controller
checks were repeated after directory-fsync recovery hardening. Existing root
Debug and ASan/UBSan suites passed **15/15 each**. During the 100-message SDL case,
maximum LVGL loop gaps were 24 ms normal and 63 ms instrumented. Intermediate
test expectations for pre-upgrade snapshots and independently generated ACKs
were corrected; no failures remained in final verification.

Run `ctest --test-dir build/simulator --output-on-failure`; sanitizer build and
startup/migration commands are in `simulator/README.md`.

Before hardware porting: implement the generic atomic storage/scan contract on
ESP-IDF (including recovery and wear leveling), secure identity/key provisioning
and backup, FreeRTOS task/queue ownership, networking, PSRAM/GUI paging and stack
budgets, then repeat real power-cut, RNG and flash-endurance tests. Corruption
backup/restore, key rotation, authenticated rollback protection and a safe
long-term relay/tombstone retention policy remain future work.
