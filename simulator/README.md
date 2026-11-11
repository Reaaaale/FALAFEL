# Desktop simulator

The simulator uses pinned LVGL 9.4.0 and SDL2 on macOS, in a fixed **466 × 466**
window. The screen uses dark sage `#34483E` with a circular, radius-clipped content container;
children cannot draw into the square window's outer corners. SDL still creates
a rectangular window: the masked corners are sage, not desktop-transparent.
Screen construction lives in `../shared/gui/`.

For a C6 lab peer, retain the simulator's existing identity/mailbox/history and
use the reciprocal device credential on C6. See [C6 lab setup](../docs/c6-relay-transport.md).
The new local integration test exchanges messages with the C6 sources compiled
on host/NVS model; it does not verify physical hardware or the Proxmox route.

## Build and run

```sh
cmake -S simulator -B build/simulator -G Ninja
cmake --build build/simulator
```

SDL2, CMake, Ninja, a C11 compiler and Python 3 are required. CMake fetches pinned
LVGL 9.4.0 and the existing Mbed TLS 3.6.7 provider, with archive hash checks.

For an explicit local test pair, provision once into a new trusted directory:

```sh
mkdir -p /tmp/falafel-e2ee-demo
./build/simulator/falafel_mailbox_fixture provision /tmp/falafel-e2ee-demo
```

This existing fixture provisioner generates separate random identities and empty
mailboxes. It refuses to overwrite existing identities. It bypasses SAS pairing;
it is test/demo provisioning, not production identity enrollment. Each 0600
`.identity` file contains the local private key followed by the paired peer key
(64 bytes total). The simulator's trusted-file adapter requires a regular,
owner-owned identity with no group/other permissions. It derives the local
public key and checks identity binding when opening the mailbox. Keychain/SAS
loading is not wired into this executable. Never send credentials to the relay.

Run these commands in three terminals from the repository root:

```sh
python3 relay/falafel_relay.py --e2ee-only --pair 1:2 --port 7777 \
  --data /tmp/falafel-e2ee-demo/relay.json

./build/simulator/falafel_simulator --device 1 --peer 2 \
  --identity /tmp/falafel-e2ee-demo/1.identity \
  --mailbox /tmp/falafel-e2ee-demo/1.mail --init-history --host 127.0.0.1 --port 7777

./build/simulator/falafel_simulator --device 2 --peer 1 \
  --identity /tmp/falafel-e2ee-demo/2.identity \
  --mailbox /tmp/falafel-e2ee-demo/2.mail --init-history --host 127.0.0.1 --port 7777
```

`--init-history` explicitly initializes/migrates a legacy v1 mailbox on the first
run. Remove it on subsequent runs; leaving it does not reset an upgraded archive.
`--history DIR` selects the local archive (default `<mailbox>.history`). Migration
imports old records, binds a random archive UUID and upgrades the local snapshot
to v2 before freeing slots. A v2 mailbox refuses a missing, corrupt or different
archive even with `--init-history`. Old binaries/headless run mode cannot use
this upgraded state. Back up identity/mailbox/history together before migration.
Legacy outgoing text was not stored: those old entries display an explicit
unavailable-text marker while retaining ciphertext, ID and delivery metadata.

Identity and mailbox paths are mandatory, must already exist, and must not be
shared between active instances. Missing, corrupt, mismatched or locked stores
fail closed; no identity/mailbox is automatically created or reset. The POSIX
backend may create its lock sidecar. `--data` legacy state is rejected.
`--device`/`--peer` also accept FALAFEL_DEVICE_ID/FALAFEL_PEER_ID; host/port and
credential paths use CLI options. There is no legacy fallback or plaintext
personality synchronization. No relay is needed to navigate with valid stores;
offline sends remain as durable history intents or active outbox entries until
a connection returns. Eight active slots are reused only after archival.

Close the window, press Control-C, or send SIGTERM to stop. SDL close/quit events
are intercepted before LVGL tears down the display: the rendering loop stays
alive until the worker finishes. Accepted commands are drained into durable history intents
when storage remains usable. Pending durable sends recover on restart. A request
accepted only into the command queue can be lost on abrupt process termination;
only the later STORED event means the application intent is durable. A queued
status follows durable active-outbox enqueue; acceptance still requires a peer ACK. Fatal identity/storage errors
stop the worker and exit the simulator with failure, without resetting state.
The existing DNS resolver is joined by the worker at shutdown; a slow OS resolver
can delay exit, while the LVGL loop remains responsive.

## Messaging controller and persistence

`src/messaging_controller.c` owns TCP, crypto, identity, mailbox and the encrypted
application history in one pthread. It reuses falafel_mail_link/falafel_mailbox,
the existing provider and desktop_transport's strict v2 frame mode. Network
frames and relay implementation are unchanged; local snapshots migrate explicitly.
Unused v1/state helpers remain a transport link dependency, never a GUI fallback.

Two fixed SPSC atomic rings connect GUI and worker: **8 commands**, **32 events**.
GUI calls never wait for worker locks, network, encryption or storage; at most
four events are applied per loop. Only the worker waits on event backpressure.
Shutdown releases that wait; a separate terminal-failure flag preserves a fatal
exit status even if presentation events are dropped while closing.

The preset `Sto arrivando` first becomes a durable application intent. The worker
uses up to eight active outbox slots, retaining exact ciphertext for retry. Peer
receipts confirm durable inbox acceptance, **not remote reading**. Local history
saves text, ID, sender/recipient, direction, delivery and local read state. GUI
rows upsert by direction/ID and restore in insertion order without duplicates.
Opening messages queues persisted local read updates, not network read receipts.

Incoming data is consumed only after an idempotent durable history effect. A
completed/consumed active slot is cleared only after its original envelope and
saved receipt are archived. Archived IDs remain available for duplicate/late-ACK
checks; exact duplicate data recovers the original authenticated receipt. No
approximate replay filter or automatic ID deletion is used.

The active mailbox remains **eight records per direction**; history supports
**1024 total IDs per paired device**. FULL never evicts messages. If active slots
are occupied, outgoing intents wait on disk. At archive capacity, new intents
fail explicitly and incoming messages remain unconsumed in active storage.
Storage corruption or uncertain commit stops the worker without resetting data.

Local records use existing AES-256-GCM, random nonces and HKDF-SHA256 domain
separation from the existing private identity. The key is not written separately;
fixture identity-file compromise still exposes history. Secure device/Keychain
provisioning, rotation and backup policy remain required for production. File
names expose IDs/direction; local rollback is not prevented. See
[application persistence](../docs/application-persistence.md) for key derivation,
blob formats, transaction/crash ordering, migration, limits and measured resources.

Text sends are valid UTF-8 without NUL, 1–117 bytes. Other authenticated binary
payloads remain stored, displaying a non-text placeholder. No demonstration
message is inserted and no session-only sent-history limitation remains.

## Character and interaction

`simulator/assets/falafel.png` is the single character for Taxi, Cuoco and
Promemoria. CMake runs `tools/embed_simulator_asset.py` to embed the original
PNG as a RAW_ALPHA LVGL descriptor in the build directory. LVGL's built-in
LodePNG decoder loads it; no runtime file path or additional image library is
needed. Replacing the PNG triggers regeneration on the next build. The earlier
`shared/assets` illustrations and their generator remain in the repository but
are not linked into this simulator view.

The 466px source is uniformly scaled into a 320 × 320 stage; it is neither
stretched nor cropped. Transparent/black margins in the source are retained.
The character leaves room for a 44 × 40 envelope button (18px icon) above its
head and a quiet 16px personality name with three 5px position dots below.
Text is cream `#F3E9D8`, accents are toasted orange `#D88A35`, and message
surfaces use muted sage `#40564A`. Backgrounds are flat, without gradients.

Drag at least 50 pixels horizontally with the left mouse button, or scroll
horizontally with two fingers on a macOS trackpad, to change personality. Vertical
gestures are ignored. Order is Taxi → Cuoco → Promemoria, bounded at the ends.
Each accepted swipe plays `falafel_rotation/frame_000.png` through
`frame_023.png` in order, for either swipe direction. A non-blocking LVGL timer
advances one frame every 30 ms (nominal 720 ms). New swipes are discarded while
playing; labels, dots and the personality callback update only after the last
frame, then the original static PNG returns. Remote synchronized state cancels
a pending local rotation without firing its callback. Trackpad input is observed
with `SDL_AddEventWatch`; LVGL still owns the SDL event loop.

Click the envelope to open the real mailbox view, or “Torna a Falafel” to
return. No demonstration message is inserted. The envelope uses toasted orange
for persistent local unread messages, muted sage otherwise. “Invia: Sto arrivando” sends
the preset; buttons and banners work during rotation. Personality changes are
local/session-only; the original synchronized-state setter remains available
for GUI tests but is not connected to a network personality event. The composer
and status-bar chrome remain unexposed in this layout.

## Asset memory and ESP32-S3 limits

CMake also runs `tools/embed_simulator_asset.py` on the rotation directory. It
validates 24 RGBA8 466 × 466 PNGs and emits compressed `const` descriptors in
`build/simulator/falafel_rotation.c`. Both generated C files are build artifacts;
source PNGs are unchanged. Runtime paths and file reads are unnecessary.

The 24 compressed frames total **2,847,217 bytes (2.72 MiB)** in read-only data,
plus 127,204 bytes for the static PNG. `LV_CACHE_DEF_SIZE=0` explicitly disables
decoded-image caching. The single software draw unit decodes only the image
currently being drawn and frees it afterwards. One RGBA frame is **868,624 bytes
(848 KiB)**; the 24-frame decoded set (19.88 MiB) is never allocated. PNG inflate
scratch space, clipping layers and display buffers are additional costs.

The desktop LVGL heap remains **8 MiB**. The smoke test measured a peak of
**about 3.75 MiB**, including snapshots, GUI objects, decode scratch
and circular layers; this excludes the separately allocated messaging worker, provider, SDL/OS
memory and mapped compressed assets.
The refresh period is 15 ms so there are two nominal refresh opportunities per
frame. The timer does not sleep, loop over frames or decode images itself;
LodePNG decoding happens synchronously during each draw. Slow decoding can
therefore extend playback rather than skip frames.

This is a bounded-memory playback strategy, not a verified ESP32-S3 port. A
hardware build must provide enough flash for the compressed assets, configure
and measure suitable PSRAM/allocation and display buffers, and benchmark PNG
decoding and circular clipping. The desktop 8 MiB heap is not an embedded RAM
budget. Lower-resolution assets, another image format or worker-assisted
single-frame decoding may be necessary to retain 600–800 ms playback on the
board. No ESP32-S3 compilation, physical display or touch verification is claimed.
The Noise K network protocol/provider and relay remain unchanged; local mailbox
retirement and the v2 persistence marker are described above.

## Verification

CI portability fix (2026-11-03): `test_history.c` checks that the sensitive marker
is absent from the binary saved record using a bounded C11 `memcmp` scan,
including the last possible offset. It no longer requires nonstandard `memmem`
or GNU feature macros (Darwin exposed that function; the Linux CI build did not).
Local Apple Clang 21 Debug build and all four simulator tests passed with SDL
dummy video. The history test also passed in a fresh ASan/UBSan build with the
messaging runtime and Mbed TLS instrumented. GNU GCC/Linux were unavailable
locally and still require GitHub Actions verification.

```sh
ctest --test-dir build/simulator --output-on-failure
# Optional rendered snapshots; create the output directory first:
mkdir -p /tmp/falafel-ui
FALAFEL_SCREENSHOTS=/tmp/falafel-ui ctest --test-dir build/simulator --output-on-failure
```

`circular_gui_smoke` launches the actual SDL window using `--device 41 --peer 42`
and `--ui-smoke-test`. It injects mouse drags and horizontal wheel events. Draw
events check all 24 frame descriptors in sequence in both navigation directions.
It checks deferred personality callbacks, overlapping-swipe rejection, message
opening while rotating, completion while the home is hidden, static restoration,
banner timeout, remote cancellation and endpoint limits. A 10 ms heartbeat must
keep running (maximum observed gap must stay below 200 ms). Snapshots check the
circular mask and character; optional BMPs include home, rotation, messages and
banner. They snapshot LVGL because SDL backbuffers are undefined after present.
Keep physical input away from the test window during automation.

On 2026-10-31 normal and ASan/UBSan builds and simulator suites passed **4/4**:
rotation smoke; encrypted_history; five controller cases; three real SDL cases.
The history tests exchange 100 consecutive messages per direction and kill
processes at 41 write/effect/consume/retirement/migration boundaries. They verify
idempotence, exact archived receipts, late ACKs, full active/history capacity,
wrong keys, corrupted records/index and archive binding. Controller cases cover
explicit legacy migration, missing/corrupt/different archives, queue saturation,
full-event-queue shutdown and runtime storage failure. SDL tests exchange 100
messages per direction, restore 200 rows/device with outgoing/incoming delivery
and local read states, replay large relay batches, restart offline endpoints and
relay, suppress duplicate rows, and close windows safely. No plaintext was found
in tested local-history/mailbox/relay records. Existing root normal and sanitizer
regressions passed **15/15 each**. Final verification had no failures.

With 200 displayed rows, the maximum measured LVGL loop gap was 24 ms normal,
63 ms instrumented; LVGL heap peak was 3,067,008 bytes. Controller/history consume
93,376 bytes excluding crypto allocations/thread stacks; the GUI ID/pointer cache
is 49,152 bytes outside LVGL. History is 800 bytes/record plus a 60+17N-byte manifest:
163,460 logical bytes at 200 records, plus the 8,392-byte mailbox. Filesystem/OS
and temporary crash files are additional. The full memory/disk table is in the
persistence document. PNG/mask snapshot smoke peak remains about 3.75 MiB.

Snapshots were inspected and git diff --check passed. SDL/socket tests require
local permissions. Existing duplicate-library linker/upstream CMake deprecation
warnings remain. Hardware, physical trackpad, actual power-cut recovery and
flash endurance are not verified. ESP32-S3 still needs atomic storage, protected
identity handling, FreeRTOS ownership, networking and PSRAM/stack budgeting.
The relay retains frames with a finite cache limit; incremental delivery streams
replay through its bounded output buffer (see `../relay/README.md`).
100 messages each direction use 400 of its default 512 cached frames.

`--ui-smoke-test` is an explicit GUI-only test mode requiring no credentials or
network; it does not send plaintext or create any state. `--test-send-preset`
invokes the normal preset action during rotation; `--test-send-count N` repeats
that action (1..1000) through the same bounded command queue; `--test-duration-ms N` injects
a window-close event after N milliseconds. The integration tests use these flags;
normal interactive operation does not require them. `messaging_probe` is a
headless controller test driver; `falafel_mailbox_fixture` reuses the existing
provisioner without importing test credential generation into the simulator.

To reproduce the sanitizer build using the already fetched LVGL source:

```sh
cmake -S simulator -B build/simulator-sage-san -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_LVGL="$PWD/build/simulator/_deps/lvgl-src" \
  -DCMAKE_BUILD_TYPE=Debug \
  '-DCMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer' \
  '-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined'
cmake --build build/simulator-sage-san
ctest --test-dir build/simulator-sage-san --output-on-failure
```
