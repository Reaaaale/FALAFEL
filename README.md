# Falafel Device

Falafel is a portable C11 application core plus an LVGL desktop simulator for a
paired-device messaging experiment. The MVP runs two simulator instances through
a self-hosted local relay, including offline delivery and synchronized
Taxi/Cuoco/Promemoria state.

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
application→transport→relay→transport→application integration test. GUI clicks
are not automated.

## Cryptographic foundation

Milestone 3A adds a portable Mbed TLS provider for secure random generation,
X25519, HKDF-SHA256, AES-256-GCM, constant-time comparison and secure erasure.
The provider is not yet connected to messages, so Falafel is not yet E2EE.
The ESP32-C6 hardware experiment and flash commands are documented in
`experiments/esp32c6-crypto/README.md`; ESP32-S3 remains the final target.

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

Use three terminals from the repository root:

```sh
python3 relay/falafel_relay.py --pair 1:2 --port 7777 --data /tmp/falafel-relay.json
./build/simulator/falafel_simulator --device 1 --peer 2 --data /tmp/falafel-a.state
./build/simulator/falafel_simulator --device 2 --peer 1 --data /tmp/falafel-b.state
```

Host and port are configurable with `--host`/`--port`; equivalents are
`FALAFEL_DEVICE_ID`, `FALAFEL_PEER_ID`, `FALAFEL_RELAY_HOST`,
`FALAFEL_RELAY_PORT`, and `FALAFEL_DATA_FILE`. Stop B, send and change personality
on A, then restart B to exercise offline delivery and alignment. Notifications
and logs show relay acceptance and device receipt as separate phases.

The `R` indicator means “relay connected”; it does not claim the peer is online.
Device state files use format v2. Unsupported or corrupt files stop startup
instead of being silently reset. The earlier MVP format v1 cannot be migrated
safely because it contains no reliable event-ID high-watermark.

## Reliability guarantees

- New relay events are acknowledged only after durable state replacement.
- Pending events remain until a valid recipient acknowledgement is persisted.
- Senders retry through disconnects and recover final status after reconnecting.
- Delivery is at least once; device and relay deduplication are explicitly
  bounded.
- Event IDs reserve persistent ranges and are not reused after normal crashes or
  restarts.
- Full relay queues apply backpressure without evicting accepted messages.

See `docs/architecture.md` for the state machine and limits and
`docs/protocol.md` for the wire format.

## MVP boundaries

Runtime traffic is unencrypted plain TCP and is suitable only for local development.
There are no production credentials, authentication, Internet deployment,
hardware drivers, or end-to-end encryption. The future security boundary is the
serialized event passed to transport. Noise-C is currently only a feasibility
candidate; it has not been integrated or approved for production use.

The future target is an as-yet-unselected ESP32-S3 touch-display board. No board
pins or drivers are assumed, and `firmware/sdkconfig` is untouched by this MVP.
