# ESP-IDF persistent storage

Implemented 2026-11-01: ESP32-C6 cross-build and host recovery tests pass.
The backend uses architecture-independent ESP-IDF APIs for a future S3 port.
**Disposable data only:** ESP-IDF 5.5.2 still has the Mbed TLS issue documented
in `PROJECT_STATUS.md`. No physical power-cut or S3 validation is claimed.

SEC-002 (2026-11-02) validates isolated IDF 5.5.6 with the corrected ECC source
as the C6 development baseline. **Permanent identities remain NO-GO**, independently
of these storage tests, until device entropy and key protection are verified.
See [SEC-002](security/SEC-002.md). The 5.5.2 results below are historical; new
storage-probe builds use [the isolated firmware commands](../firmware/README.md).

## Choice and contract

| Option | Assessment |
|---|---|
| Dedicated NVS partition: selected | Included in ESP-IDF, with synchronous blob writes, CRCs and page recovery. Existing snapshot/manifest sizes use multi-page blobs. Requires verification of publication/recovery, GC headroom and wear; not a multi-key transaction. |
| LittleFS | Suitable power-loss-resilient copy-on-write design; would require a pinned additional ESP-IDF component and validation of sync/rename/VFS semantics, locking and scan. Candidate for larger archives, not added here. |
| SPIFFS | Documented power-loss corruption and GC behavior do not directly satisfy the atomic-replacement contract. |
| FAT with wear levelling | Does not itself supply crash-atomic replacement/directory publication; would require an additional transaction scheme. |
| Raw partition journal | Would duplicate recovery, integrity and wear management; unnecessary for the initial bounded archive. |

In the inspected IDF 5.5.2 implementation, new blob chunks precede publication
of a blob index. `nvs_set_blob` already writes flash; `NVSHandleSimple::commit()`
is currently a no-op. The adapter nevertheless calls commit as required by the
public API and treats every failed write as potentially committed. It never
assumes an error means that no data reached flash.

References: [NVS](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/storage/nvs_flash.html),
[SPIFFS](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/api-reference/storage/spiffs.html),
[LittleFS](https://github.com/littlefs-project/littlefs).

## Layout and API

`firmware/components/falafel_storage/` implements existing
`falafel_mail_storage` and `falafel_history_storage` callbacks. One dedicated
partition contains one `falafel` namespace for one mailbox/archive pair:

| NVS key | Unchanged value |
|---|---|
| `mailbox` | FMBX v1/v2 snapshot: 8,376 / 8,392 bytes |
| `index` | AES-GCM FHI1 manifest: 60 + 17 × count bytes |
| `r0000` ... `r1023` | AES-GCM FHR1 record: 800 bytes |

The 15-character NVS key limit does not truncate IDs. Open enumerates committed
records and rebuilds a full `(direction, ID)` RAM map from their existing FHR1
headers. New IDs use unused slots; updates reuse the original slot. No slot is
deleted. Unknown keys/types, malformed record headers and duplicate logical IDs
fail closed. Shared core open then verifies GCM, sequences, manifest membership,
identity binding and mailbox checksum; backend open alone is not authentication.
History scan enumerates cached logical names without holding a live NVS iterator
while the core repairs the manifest. Committed orphans remain discoverable.

No pairing, E2EE, message or FMBX/FHI1/FHR1 encoding changed. NVS is a different
physical container, not a POSIX directory. No directory import/export or existing
data migration is implemented.

## Ownership and initialization

Zero-initialize the context and allocate it statically/on the heap. A process-wide
atomic guard rejects concurrent backend owners, including double-open of one
context. The partition is dedicated and must not be initialized/accessed by other
components. The owning worker serializes callbacks and close. Flash operations
are synchronous and must never run in the LVGL thread.

Normal startup uses `falafel_storage_espidf_open(..., false)`. Missing namespace
is explicit. `true` only authorizes namespace creation during explicit
provisioning; it creates no mailbox, history or identity. Core create flags remain
separate and must also be false on ordinary startup. An existing v2 mailbox must
be validated before deciding whether an archive can be created: never substitute
a newly initialized archive UUID for a missing required archive.

There is no automatic erase/format, including NO_FREE_PAGES, NEW_VERSION_FOUND
or initialization failure. NVS initialization may perform its own recovery/GC
writes: the boot probe is not a forensic read-only flash mount. Capacity failure
does not evict history. Errors remain visible in return values/`last_error`.
Load retains 0/success, 1/absent, -1/error; a key known at open disappearing is
an error. A write failure poisons the adapter/shared handle. Close/deinitialize,
reinitialize and reopen both stores before retrying; the failed operation may
have committed. Close does not rescue/commit a failed pending operation.

## Atomicity and safe consumption

Host tests establish adapter/core behavior under modeled single-blob outcomes:

- Success only follows successful set and commit.
- Complete OLD or NEW states and errors after publication are recovered.
- Partial unpublished fragments are not exposed by the API model.
- Authenticated orphan records are recovered into the existing manifest.
- The test consumer commits history before consuming the inbox, then retains
  exact ciphertext/ACK before reclaiming a slot; replay is idempotent.
- Retired duplicates return the exact original authenticated ACK.

No multi-key transaction is added. Existing core ordering provides crash recovery
between record, manifest and mailbox. The firmware stub only probes an existing
namespace; it has no messaging consumer and invokes no crypto at boot.

Physical validation is still required for real blob-index/page recovery under
interrupted programming, erasure and GC. CRC/GCM detect corruption, not repair
lost media, prevent authenticated rollback or guarantee survival of arbitrary
flash damage. NVS can make damaged entries inaccessible; startup must not treat
absence as permission to recreate them.

## Memory, capacity and keys

Host ABI sizes: backend **17,444 bytes** (17,408-byte full-ID map plus state),
shared history **55,520**, mailbox **16,792**. The adapter allocates no heap;
NVS does. Measure actual target ABI, heap and stack separately.

The experimental NVS partition is **1 MiB** in a **2 MiB storage-only** layout.
At 1,024 messages, logical history is 836,668 bytes plus an 8,392-byte mailbox,
excluding NVS overhead, old/new replacement headroom and spare GC pages. The
model proves the logical slot limit, not that every workload fits physical 1 MiB
NVS. Earlier ENOSPC is explicit backpressure. Growing manifest rewrites and status
updates amplify writes; measure capacity, latency and wear before production.

The backend stores no private key: history values are already AES-GCM encrypted
by the shared core, and mailbox values retain existing encrypted envelopes.
IDs/direction/count remain visible. Protected identities, NVS encryption,
backup/rotation and anti-rollback are not supplied. Limits remain eight active
slots/direction and 1,024 lifetime archived IDs, without pruning. Relay unchanged.

## Verification and physical checklist

The actual adapter is compiled by `test/test_storage_espidf.c` against the NVS
API model in `test/storage_nvs_model/`, real shared core and host Mbed TLS 3.6.7.
Fresh test identities exist only in process RAM; no private-key blobs are saved
and no relay is contacted. The model does not emulate page CRCs, flash erasure,
GC, timing or wear. Reopen is a modeled reboot, not a physical reset.

Executed 2026-11-01: normal and ASan/UBSan root suites passed **16/16 each**:

- Exact formats, explicit creation, exclusive owner and encrypted values.
- 100 encrypted receives/reopens, persistent reads, idempotence/conflicts and
  exact duplicate ACKs after retirement/restart.
- 30 interruption/error scenarios spanning inbox acceptance, record, manifest,
  consume, retirement and reclaim, including orphan recovery.
- GCM/checksum/missing-entry/header corruption, duplicate physical aliases,
  malformed keys/types and initialization failures without reset.
- Modeled ENOSPC, 1,024 records without eviction, updates at capacity and eight
  active mailbox slots; existing TCP/E2EE regressions also pass.

C6 cross-build passed with ESP-IDF 5.5.2: **0x2d660-byte** firmware fits its
0xf0000 factory partition. No device was flashed. S3 toolchain is not installed;
its build was not attempted. See [build commands](../firmware/README.md).

Before hardware approval, add/run a disposable-data device harness and verify:

1. Write/read across software reset, cold boot and actual power removal.
2. Power cuts during mailbox/manifest/record replacement and NVS GC: complete
   OLD/NEW state or explicit failure, never consume without durable history.
3. Exact DATA/ACK replay before/after consume, reclaim and reboot.
4. Real full-partition/update headroom near 1,024 IDs; old records remain readable
   after write failure, without erase/reset.
5. Controlled corruption, missing keys and invalid NVS versions fail closed.
6. Heap, largest block, task stack, startup/write latency and endurance; repeat
   on S3 and the final flash chip/partition layout.
7. Encrypted integration only after SDK correction and boot entropy verification,
   using exclusively disposable test identities.
