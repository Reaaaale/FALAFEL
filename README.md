# Falafel Device

Falafel is a portable C11 application core plus an LVGL desktop simulator for a
paired-device messaging experiment. Two simulator instances now exchange E2EE
mailbox messages through a self-hosted relay, with offline recovery.
Taxi/Cuoco/Promemoria selection is local in the encrypted simulator.

The ESP32-C6 lab firmware now integrates the existing E2EE mailbox/link with
Wi-Fi/TCP, explicitly provisioned identities and NVS. Host/SDL integration and
C6 cross-build pass. The user confirms successful real bidirectional
C6 -> Proxmox -> Mac E2EE. The reset/reconnect fix is locally verified;
deployment to LXC 100 and hardware reset acceptance remain pending.
See [C6 lab build, provisioning and acceptance test](docs/c6-relay-transport.md).

## Build and test

```sh
cmake -S . -B build/tests -G Ninja -DFALAFEL_ENABLE_SANITIZERS=ON
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure

cmake -S simulator -B build/simulator -G Ninja
cmake --build build/simulator
```

The simulator configure downloads pinned LVGL 9.4.0 and requires SDL2. Core
tests need CMake, a C11 compiler, and Python 3 for relay integration.
The crypto target downloads the pinned official Mbed TLS 3.6.7 release archive
and verifies its SHA-256 hash.

The test suite includes relay fault injection and a headless C
application→transport→relay→transport→application integration test. The simulator
has SDL input/rendering, controller and two-window E2EE integration tests; see `simulator/README.md`.

## Cryptographic foundation

Milestone 3A adds a portable Mbed TLS provider for secure random generation,
X25519, HKDF-SHA256, AES-256-GCM, constant-time comparison and secure erasure.
The headless runtime and LVGL simulator use this provider through the existing
Noise K mailbox/link for E2EE messages. The simulator has no event-v1 fallback.
The ESP32-C6 hardware experiment and flash commands are documented in
`experiments/esp32c6-crypto/README.md`; ESP32-S3 remains the final target.

SEC-002 validates the isolated **ESP-IDF 5.5.6 / Mbed TLS 3.6.7 development
baseline for C6**, including corrected ECC source and linked code, clean C6
builds and host/E2EE/sanitizer regressions. Use the child-process wrapper in
[the firmware guide](firmware/README.md); the old global 5.5.2 installation is
retained unchanged. **Permanent real identities remain NO-GO on C6/S3** until
device entropy and key protection are verified. No provider/protocol change.
See [SEC-002 results](docs/security/SEC-002.md) and the historical
[SEC-001 assessment](docs/security/SEC-001.md).

[SEC-003](docs/security/SEC-003.md) prepares the existing C6 experiment for real
hardware: 14 device checks compiled, focused host and ASan/UBSan suites passing,
and an exact manual flash/UART/reboot procedure. **Hardware tests NOT RUN;
SEC-003 remains OPEN and permanent identities NO-GO.** No flash was executed.

Milestone 3B.1 evaluates the exact suite
`Noise_XX_25519_AESGCM_SHA256` in isolated macOS and ESP32-C6 experiments.
Desktop and independent-vector tests and the C6 cross-build pass; physical C6
execution and a backend that reuses the 3A primitives remain required before
adoption. See `experiments/noise-feasibility/README.md`.

Milestone 3B.2 adds a portable desktop pairing state machine and two-process
macOS harness, including a 12-digit SAS and authenticated bilateral approval.
It remains isolated from GUI, relay, event-v1 messaging and persistence. See
`experiments/desktop-pairing/README.md`.

Milestone 3B.3 persists the local identity and verified peer across desktop
restarts. The private X25519 key is stored in macOS Keychain; atomic metadata
contains only public state. Corrupt and interrupted saves fail closed, and
explicit unpair may rotate the identity. The pairing remains outside event-v1
and application E2EE.

## Run two devices

The simulator requires separate existing identity/mailbox files. For local tests,
explicitly provision one random demo pair once into a new trusted directory:

```sh
mkdir -p /tmp/falafel-e2ee-demo
./build/simulator/falafel_mailbox_fixture provision /tmp/falafel-e2ee-demo
```

Then run the relay and the two devices in separate terminals:

```sh
python3 relay/falafel_relay.py --e2ee-only --pair 1:2 --port 7777 \
  --data /tmp/falafel-e2ee-demo/relay.json
./build/simulator/falafel_simulator --device 1 --peer 2 \
  --identity /tmp/falafel-e2ee-demo/1.identity --mailbox /tmp/falafel-e2ee-demo/1.mail --init-history
./build/simulator/falafel_simulator --device 2 --peer 1 \
  --identity /tmp/falafel-e2ee-demo/2.identity --mailbox /tmp/falafel-e2ee-demo/2.mail --init-history
```

Open the envelope and choose “Invia: Sto arrivando”. Missing/corrupt credentials
or mailbox stop startup; the simulator never provisions or resets them and
rejects legacy `--data`. `--init-history` explicitly creates the application archive and migrates local
mailbox v1 to archive-bound v2 before slot reuse. Later starts can omit it; a
missing/wrong upgraded archive is never reset. `--history DIR` overrides the
default `<mailbox>.history`. Keep identity, mailbox and archive together.
The fixture bypasses SAS pairing and stores private
keys in local 0600 files; it is explicitly test/demo enrollment. Keychain loading
is not integrated. The relay receives no private keys. See
[the simulator guide](simulator/README.md) for flags, shutdown, architecture and
limits. Stop/restart either device or the relay to exercise offline recovery.

## Reliability guarantees

- GUI commands are volatile requests; STORED confirms a durable history intent.
  Active-outbox enqueue follows. Authenticated peer receipts mean durable inbox
  acceptance, never remote reading.
- Exact ciphertext retry, reconnect and saved receipts recover pending delivery.
- Relay cached replay/live delivery waits for space in its 65,536-byte output
  buffer; slow readers do not need the backlog to fit at once. Cache GC remains
  unimplemented. See [relay regression coverage](relay/README.md).
- A dedicated worker owns crypto, network and storage. Bounded atomic queues let
  LVGL process events without waiting for those operations.
- AES-GCM application history restores sent/received text, delivery and local read
  states; ID-based GUI upserts suppress duplicates after replay/restart.
- Receive consumption follows durable idempotent history commit. Active slots are
  freed only after archiving exact ciphertext/receipts; archived IDs are retained.
- Eight active records per direction are reusable. The current archive limit is
  1024 total IDs/device; history/relay FULL gives explicit backpressure, no eviction.
- Local snapshot v1 migrates explicitly to archive-bound v2 (8376 -> 8392 bytes).
  Old outgoing text not previously persisted is marked unavailable during import.

See `docs/architecture.md` for the state machine and limits and
`docs/protocol.md` for the wire format.

## MVP boundaries

The simulator and headless runtime protect mailbox message content end to end,
but remain local development prototypes: TCP routing/registration are not
authenticated and have no TLS. Production identity enrollment, networking/display adapters and rollback protection are
not supplied. The encrypted local history key derives from the existing private
identity; fixture key-file compromise exposes history. See
[the persistence/key-management design](docs/application-persistence.md). The legacy core
and mixed relay mode remain available for existing tests; the GUI does not use
them as a messaging fallback. Noise-C pairing/live-IK experiments remain separate.

The future target is an as-yet-unselected ESP32-S3 touch-display board. No board
pins or drivers are assumed, and `firmware/sdkconfig` is untouched by this MVP.

An experimental ESP-IDF NVS backend implements existing mailbox/history callbacks
without format changes. C6 cross-build and normal/sanitized host recovery tests
pass; physical power-cut/GC and S3 verification remain pending. No worker or
identity provisioning is added; the embedded Mbed TLS issue still excludes real
data/identities. See [firmware commands](firmware/README.md) and
[storage guarantees and limits](docs/embedded-storage.md).

The isolated Milestone 3C.2A offline encryption module now provides authenticated
Noise K envelopes using the shared provider and persisted pairing identities.
It supports up to 122 plaintext bytes within 256-byte envelopes. It is connected to the headless 3C.2C runtime through the mailbox. See [the design and test commands](docs/async-e2ee-design.md)
for its API, optional Noise-C interoperability suite and security limitations.

Milestone 3C.2B adds an isolated persistent mailbox with a portable storage API,
a POSIX atomic-file backend, eight legacy lifetime records per direction, exact-byte
retries and authenticated durable-acceptance receipts. Its typed data payload
allows 117 application bytes. Application consumption uses an explicit
peek/consume contract requiring idempotent effects after crashes. Root CTest
includes `mailbox_persistence`, with process-crash and storage-failure tests.
The headless runtime integration is provided by 3C.2C; see the same design document.


Milestone 3C.2C integrates encrypted mailbox data and authenticated peer receipts
with the existing nonblocking TCP adapter and durable Python relay, using explicit
transport v2 while retaining v1 compatibility. Start the relay with `--e2ee-only`
for this path. The headless peer includes an idempotent durable hash-index
consumer. LVGL now reuses the same mailbox/link in a separate worker, committing
encrypted application history before consumption and archiving IDs/receipts
before active-slot reuse. See [run commands and fixture provisioning](experiments/desktop-mailbox/README.md).
`encrypted_tcp_end_to_end` tests separate A/B/relay processes, offline handoff,
relay restart, recovered receipts, partial I/O, forged payloads and capacity.


The first circular Falafel UI now runs in a 466 × 466 SDL window: the supplied
PNG is scaled to a 320px stage on dark sage with cream text and toasted-orange
accents; swipes select Taxi/Cuoco/Promemoria, and the smaller envelope
opens the real mailbox view, with preset sending and a return button. The same PNG serves all three
personalities; swipes now play a 24-frame prerendered rotation before committing
the new selection, with decoded-image caching disabled. Build/run commands,
memory estimates and the GUI smoke test are in
[the simulator guide](simulator/README.md). Messaging uses the E2EE worker;
no PNG, protocol, relay implementation or cryptographic changes were required.
