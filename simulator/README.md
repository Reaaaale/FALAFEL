# Desktop simulator

The simulator uses pinned LVGL 9.4.0 and SDL2 on macOS, in a **466 × 466**
window by default (`--display-size 320..800` selects a prototype diameter). The screen uses dark sage `#34483E` with a circular, radius-clipped content container;
children cannot draw into the square window's outer corners. SDL still creates
a rectangular window: the masked corners are sage, not desktop-transparent.
Screen construction lives in `../shared/gui/`.

For a C6 lab peer, retain the simulator's existing identity/mailbox/history and
use the reciprocal device credential on C6. See [C6 lab setup](../docs/c6-relay-transport.md).
The new local integration test exchanges messages with the C6 sources compiled
on host/NVS model; it does not verify physical hardware or the Proxmox route.

## Optional mTLS

The existing GUI/mailbox/controller flow is unchanged. Supply all four options:
`--tls-ca /external/ca.pem --tls-cert /external/simulator.pem
--tls-key /external/simulator.key.pem --tls-server-name falafel-relay.lab`.
The CA/name verification and client authentication use the existing Mbed TLS
3.6.7 dependency. The expected TLS name/IP-SAN is independent of the connection
`--host`, permitting the reviewed SSH tunnel. Key files must be owner-owned,
regular, private (0600 or stricter), and stay external to the repository.
Incomplete/empty options or verification errors never fall back to TCP. Omit
ALL TLS options only when explicitly selecting the existing TCP lab configuration.

Retain existing `--identity`, `--mailbox` and `--history`, with no reinitialization
or concurrent writer. TLS cert/key differ from both C6's TLS key and the existing
E2EE identity. See [complete credentials, hardware trial and rollback commands](../docs/mtls.md).
Software normal/sanitized checks pass; physical mTLS is not yet tested.

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

The 72 Blender PNGs live under
`simulator/assets/falafel_sprites/falafel_sprites/{taxi,cuoco,promemoria}/`,
with `frame_00.png` through `frame_23.png` in each directory. The nested directory
is the actual imported asset location. CMake embeds the originals as compressed
RAW_ALPHA LVGL descriptors, indexed by personality and angle. LVGL's built-in
LodePNG decoder needs no runtime file paths or additional image library. All 72
files are build dependencies. The older `falafel.png`, `falafel_rotation/` and
`shared/assets` illustrations remain in the repository but are not linked here.

All supplied canvases are currently 466 × 466 RGBA8. The converter reads their
actual dimensions and requires a common canvas across outfits, without fixing
them to 466px. This is an asset/simulator size, **not a selected hardware display**;
the circular device model and final display resolution remain undecided.
At the default 466px diameter the source canvases share one uniform scale into a 320 × 320 stage, with original
transparent margins and no per-outfit resizing or cropping. The common stage
is centered 32px below the screen center (28px below the earlier layout), leaving
the chef hat below the envelope and the Taxi car above the personality label.
Alpha bounds were checked for all 72 frames: none touches a canvas edge;
Taxi's union is x=79..386, y=81..362. Visual contact sheets confirm consistent
body size. The 44 × 40 envelope, 18px icon, 16px label and three 5px dots remain.
Text is cream `#F3E9D8`, accents toasted orange `#D88A35`, and message surfaces
muted sage `#40564A`; backgrounds remain flat without gradients.

Drag at least 50 pixels horizontally with the left mouse button, or scroll
horizontally with two fingers on a macOS trackpad, to change personality. Vertical
gestures are ignored. Order is Taxi → Cuoco → Promemoria, bounded at the ends.
Each accepted swipe reuses the existing 24-frame rotation as a circular sequence:
left selects the next personality with a frame step of -1; right selects the
previous personality with a frame step of +1, modulo 24. The existing
non-blocking LVGL timer targets a **280 ms full turn**, returning to the exact
starting pose after all 24 steps. Absolute deadlines distribute intervals of
11/12 ms without rounding drift; delayed callbacks never skip frame indices.
When home is visible, the next step waits for the current frame's draw event.
If rendering cannot keep up, playback extends rather than dropping frames. The current frame is retained between animations;
there is no forced switch to the static PNG on completion/cancellation and no
reset to frame zero when starting another turn. Initial creation uses frame zero
of the current personality. A turn starts in the current outfit, then switches
to the destination outfit exactly at rear-view **frame 12**, keeping the same
angular index. That outfit remains latched through wraparound to completion;
application personality, label, dots and callback still change only at the end.

Labels, dots and the personality callback commit exactly once at completion.
New swipes are discarded while playing, with no queue or overlapping timer.
A mouse drag begun while busy remains blocked even if released after playback.
Trackpad events from a busy gesture, including subthreshold deltas and its
inertial tail, stay latched until 220 ms without wheel events; a fresh gesture
can then be accepted. SDL natural-scrolling inversion is normalized before
choosing the direction. Remote synchronized state cancels a pending choice
without a local callback, retaining its angular pose and restoring the remotely
selected outfit, even if the selected personality has not changed.
Playback still completes while messages are open. Trackpad input is observed
with `SDL_AddEventWatch`; LVGL still owns the SDL event loop. The watch reads
an atomic busy/visibility snapshot published by the GUI thread, not LVGL objects.

Press **F2** with the simulator window focused (Fn+F2 if your keyboard uses
media keys) to simulate the ESP32-S3 physical menu button and open three entries: **Messaggi**, **Giochi** and
**Impostazioni**. All pages use the existing sage/cream/orange palette and
Montserrat fonts. The menu has a central column of three buttons and a “Home”
back button; entry pages have a “Menu” back button. Click Messaggi or the existing
envelope to open the real mailbox view: “Indietro” returns to the route used to
enter (Menu or Home). Giochi and Impostazioni explicitly say that their contents
are not yet available; there are no simulated games, settings or working-looking
switches. Opening the menu alone does not mark messages read; entering Messaggi
retains the existing local-read callback and unread behavior. Reopening the same
active route is idempotent. There is no touch Menu button on Home. F2 opens
Menu from any page, including during rotation; holding it does not repeat or
toggle the menu. Internal entries and back controls remain touch-operated.
The window title includes the F2 shortcut.

`src/menu_button.c` observes SDL key edges for this window, rejects repeat/held
events and clears held/pending state on focus loss. It latches atomics in the
SDL watch and dispatches `falafel_gui_menu_button_pressed()` on the LVGL thread;
it does not create another SDL event loop. The shared hook expects one debounced
physical press on the GUI task. S3 GPIO selection, pull-up/polarity, debounce
and task/ISR delivery are pending the final board and firmware GUI integration;
no hardware button driver or physical validation is claimed.

`shared/gui/falafel_navigation.c` supplies reusable page, button, back-navigation
and entry-page primitives. `falafel_gui.c` owns the typed page router and one
message-origin value; pages are built once and shown/hidden without an unbounded
history stack. The legacy composer remains unexposed; its back action returns to
Messages. Shared navigation uses C11/LVGL only, with no SDL/worker/storage calls.
The circular root takes the smaller display dimension; new page geometry uses
parent-relative percentages. Home retains reference framing at 466px and scales
the common stage/offsets for other prototypes, anchoring the fixed-size envelope
above the chef hat. Fonts and source assets are unchanged. Touch target sizing,
font legibility, RAM/PSRAM and rendering on the final physical display still need
hardware-specific validation; `--display-size` is not a hardware selection.

Personality input is accepted only while Home is active. Menu and entry views
do not register the Home gesture handlers; pointer releases on the envelope
respect the consumed-swipe guard. The existing SDL wheel visibility/busy
latch also blocks hidden-Home gestures and their inertial tails. A pending
rotation can finish while another route is visible, preserving its callback and
pose semantics.

No demonstration message is inserted. The envelope uses toasted orange
for persistent local unread messages, muted sage otherwise. “Invia: Sto arrivando” sends
the preset; buttons and banners work during rotation. Personality changes are
local/session-only; the original synchronized-state setter remains available
for GUI tests but is not connected to a network personality event. The composer
and status-bar chrome remain unexposed in this layout.

## Asset memory and ESP32-S3 limits

CMake runs `tools/embed_simulator_asset.py` on the nested sprite directory. It
validates 3 × 24 RGBA8 PNGs with identical nonempty dimensions and emits compressed
`const` descriptors in `build/simulator/falafel_rotation.c`. The generated C file
is a build artifact; source PNGs are unchanged. Runtime file reads are unnecessary.

The 72 compressed frames total **8,125,811 bytes (7.75 MiB)** in read-only data;
the old static PNG/rotation sequence is no longer linked. `LV_CACHE_DEF_SIZE=0`
explicitly disables decoded-image caching. The single software draw unit decodes
only the image currently being drawn and frees it afterwards. One current RGBA
frame is **868,624 bytes (848 KiB)**; the complete 72-frame decoded set (59.64 MiB)
is never allocated. PNG inflate scratch, clipping layers and display buffers
are additional costs.

The desktop LVGL heap remains **8 MiB**. The smoke test measured a peak of
**about 3.75 MiB**, including snapshots, GUI objects, decode scratch
and circular layers; this excludes the separately allocated messaging worker, provider, SDL/OS
memory and mapped compressed assets.
The refresh period is 5 ms, shorter than the 11/12 ms rotation steps. The timer
does not sleep, loop over frames or decode images itself; LodePNG decoding
happens synchronously during each draw. LVGL is compiled with `-O2` on Clang/GCC,
including debug builds, so its PNG decode, scaling/blending and memory routines
can sustain the 280 ms target. Debug symbols, assertions and sanitizer flags
are retained; application/transport compilation flags, decoder source,
decoded-image cache policy and the 8 MiB budget are unchanged.
Slow machines or sanitizer instrumentation can extend playback; all visible
frames are still drawn in order.

This is a bounded-memory playback strategy, not a verified ESP32-S3 port. A
hardware build must provide enough flash for the compressed assets, configure
and measure suitable PSRAM/allocation and display buffers, and benchmark PNG
decoding and circular clipping. The desktop 8 MiB heap is not an embedded RAM
budget. Lower-resolution assets, another image format or worker-assisted
single-frame decoding may be necessary to retain the 280 ms playback target on the
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

The three additional CTests `circular_navigation_320`, `circular_navigation_466`
and `circular_navigation_480` inject real SDL key/click/drag/wheel events. They
check all menu entries and return paths, Home envelope compatibility, unread and
callback timing, no personality change from hidden-Home input, a swipe across
the former Home Menu position, pose continuity, and 100 repeated route cycles.
They also check absence of the Home Menu target, F2 held/repeat/foreign-window
rejection, focus-loss recovery, opening from Settings and during rotation. Every visible
navigation button's full rectangle must fit the physical circle. Optional
`FALAFEL_SCREENSHOTS` captures each entry page at all three diameters.

```sh
ctest --test-dir build/simulator -R '^circular_navigation' --repeat until-fail:3 --output-on-failure
# Automatic visible navigation demo, no credentials/controller/network:
env -u SDL_VIDEODRIVER build/simulator/falafel_simulator \
  --device 41 --peer 42 --ui-navigation-test --display-size 466
```

2026-11-16 physical-button update: normal and ASan/UBSan builds compile and
complete simulator suites pass **7/7 each**, including E2EE integrations. Normal
navigation tests additionally pass three consecutive runs per diameter. No
physical S3 run or firmware GUI build was performed.

2026-11-16 navigation milestone: normal and ASan/UBSan builds compile; both full
simulator suites pass **7/7**, retaining rotation and encrypted-history/controller/
SDL integration regressions. Navigation passes three consecutive runs per size
in each build. Host sanitized C6/simulator plus relay mTLS regressions pass 2/2.
Snapshots at 320/466/480 were produced; Home/Menu/Messages/entry layouts were
visually reviewed, including smaller-screen envelope/chef-hat clearance. No
firmware, identity, protocol or transport changes and no physical S3 validation.

`circular_gui_smoke` runs the actual simulator/LVGL renderer with SDL's dummy
video driver under CTest, avoiding interference from physical desktop input.
It injects mouse drags and normal/flipped horizontal wheel events. Actual draw
events check all 24 frame descriptors in opposite cyclic orders. It checks
left/right return to the starting pose, eleven committed transitions, rapid busy
swipes, a drag held across completion, a subthreshold busy burst and its tail
across completion, limits, remote cancellation/resumption from a nonzero pose,
single callbacks and message/banner availability. Each observed draw checks the
source outfit before frame 12, destination outfit afterwards and unchanged
application personality while busy. All four adjacent transitions, consecutive
turns and return paths are covered; Taxi ↔ Promemoria goes through Cuoco, retaining
the existing bounded navigation. Test timeout is 40 seconds.
The 10 ms heartbeat must keep running throughout: maximum gap <=200 ms normally;
CMake builds using `-fsanitize` use 600 ms because instrumented synchronous PNG
decoding is not a release-performance measurement. Runtime animation timing and
assets are identical between these builds. PNG decode still extends playback
rather than skipping frames. Optional BMP snapshots cover home, rotation,
messages/banner, left/right return, Cuoco/Promemoria and reverse rotation after cancellation.
They snapshot LVGL because SDL backbuffers are undefined after present.

Outfit verification (2026-11-16): both simulator builds and full suites pass
**4/4 normal and 4/4 ASan/UBSan**. Final normal direct turns measured 280–281 ms;
instrumented visible turns take roughly 0.53–0.60 s, retaining all frames.
Final GUI heartbeat maxima were 24/44 ms, with LVGL heap peak 3,936,064 bytes.
Inspected rendered Taxi/Cuoco and sprite contact sheets: car and hat fit, with
shared body scale and unchanged palette. A synthetic 20 × 30 canvas also embeds
successfully; inconsistent dimensions are rejected. Final normal integration
reruns required local-socket access after the sandbox denied `bind(127.0.0.1)`.
Hardware display/touch/performance remains unverified.

Earlier timing verification (2026-11-13): normal and ASan/UBSan builds and full
simulator suites **4/4 each PASS**. The expanded smoke checks all 24 rendered
frames, both directions, pose retention, busy/held swipes, inertial tails and
single selection callbacks with shorter busy-check timings. Normal full-turn
measurements from the direct request were 280–281 ms; the final snapshot run
measured 280 ms in all three timed cases. The normal test allows 80 ms of
scheduling/render tolerance for these measurements and rejects the old 720 ms
behavior. Sanitizer builds check frame completeness/liveness, not release-speed
performance (roughly 0.55–0.60 seconds per visible turn in the latest full suite).
Final normal/sanitized heartbeat maxima were 24/89 ms; LVGL heap remains about
3.75 MiB. New initial/round-trip BMP pixels are equal and match the prior GUI's
initial snapshot despite renderer optimization. No real identities or stores
were accessed. The 280 ms target is not a hard real-time guarantee on every
machine/display; physical trackpad and S3 performance still need manual review.

Earlier direction-only verification on 2026-11-13: the requested final mapping (left -1/right +1)
passes circular_gui_smoke in normal and ASan/UBSan builds, including unchanged
personality order, pose retention and busy input handling. Earlier verification:
normal simulator suite 4/4 PASS. All four sanitizer
checks pass after correcting the smoke timing budget (the GUI case was rerun
separately; its initial 200 ms budget failed at ~235 ms, with no ASan/UBSan
report). Normal GUI smoke passes three consecutive runs: heartbeat maxima
89/70/74 ms, peak LVGL heap about 3.75 MiB. Sanitized GUI smoke PASS: 260 ms,
29.27 seconds total. Initial/home-left-right BMP pixel data are exactly equal;
normal and reverse screenshots were inspected. Host C6/simulator E2EE and
relay mTLS regressions 2/2 PASS, using temporary stores/credentials only.
The new direction-order assertion failed on the original implementation.
Native macOS window startup was also exercised; physical trackpad calibration/
momentum remains a manual check, not claimed by injected SDL tests.

For a visible, automatic GUI-only demonstration (no identity, mailbox or relay):

```sh
cmake --build build/simulator -j4
env -u SDL_VIDEODRIVER build/simulator/falafel_simulator \
  --device 41 --peer 42 --ui-smoke-test
```

The window closes automatically after the scenarios. Do not add physical input
while that automation is running. To try swipes manually, relaunch your existing
normal simulator command with the same identity/mailbox/history and TLS options
from the sections above; only the rebuilt executable changes. No new E2EE
fixture, history initialization or credential provisioning is needed.

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
