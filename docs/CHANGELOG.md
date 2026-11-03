# Falafel development changelog

## 2026-11-03 — SEC-003 preparation revalidation; hardware still OPEN

- Found and reused the existing SEC-003 experiment, suite, probe and log tools;
  preserved all unrelated uncommitted changes. No production provider, protocol,
  EMB-001/002, SDK, permanent identity or device changes.
- Extended tools/verify-sec003.py with --build-root for retained, independent
  build sets, explicit safety configuration and exercised provider-symbol checks.
- Fresh isolated ESP-IDF 5.5.6 C6 build PASS (248,208 bytes), native 3/3 and
  vendor-inclusive ASan/UBSan 3/3 PASS, including 14 synthetic parser tests.
  Offline source/configuration/link verifier and git diff --check PASS.
  Initial sandbox psutil/sysctl failure resolved by approved external build.
- New artifact/evidence root: build/sec003-20261103; all earlier builds retained.
  Updated SEC-003 report/hashes/commands, experiment and firmware guides,
  project status and this changelog. Hardware execution remains NOT RUN;
  GO for disposable physical tests, NO-GO for permanent identities. Next gate:
  operator logs from five warm and five cold runs, followed by review.

## 2026-11-02 — SEC-003 software preparation; hardware validation OPEN

- Reused experiments/esp32c6-crypto with the actual unchanged production provider.
  Added portable known-answer/ephemeral/tamper/error/cleanup tests, observed
  automatic reseed, experiment-only entropy failure injection and hardware-poll
  counting. No secret/random samples are logged; reboot labels hash public keys.
- Added isolated safety SDKCONFIG overlay, a 10-second capture delay, no-reset
  flash behavior, per-case/resource logs and saved-log analyzer with ELF binding.
  No persistent identities, production RNG changes or new protocol/dependency.
- Clean ESP-IDF 5.5.6/Mbed TLS 3.6.7 C6 build PASS, 248,208-byte binary in
  build/sec003/crypto-c6-ready; previous configs/builds retained. Read-only
  verifier confirms compiled/linked callback routes, ECC fix and configuration.
- Focused native normal 3/3 and ASan/UBSan 3/3 PASS: 13 portable cases, existing
  raw provider regression, 14 synthetic parser tests. git diff --check PASS.
  These do not execute hardware or prove entropy. EMB-001/002 edits untouched.
- Documented RNG/RF/SAR/ADC/power-state limits and exact manual UART/flash/reboot
  protocol in docs/security/SEC-003.md; aligned firmware/experiment/main guides,
  architecture and project status. No flash/monitor/eFuse/provisioning performed.
- Software readiness GO; SEC-003 remains OPEN pending physical five warm/five
  cold runs and review. Permanent identities remain NO-GO on C6/S3.


## 2026-11-02 — SEC-002 isolated ESP-IDF 5.5.6 development baseline

- Installed complete official v5.5.6 SDK and recursive submodules in
  .cache/sec002/esp-idf-v5.5.6, separate tools and Python venv under
  .cache/sec002/tools, without changing/removing SDK 5.5.2 or shell profiles.
- Verified release/submodule pins, advisory fix ancestry, fixed source SHA256,
  actual compilation paths/macros and the corrected Curve25519 reduction call
  in the C6 experiment ELF. Mbed TLS 3.6.7 and GCC esp-14.2.0_20260121; official
  downloaded tool archive hashes verified. No crypto implementation changes.
- Added child-process environment wrapper and read-only evidence verifier.
  Fresh embedded configs/builds and host builds use build/sec002; host Mbed TLS
  sources come directly from the candidate SDK, with its isolated Python.
- Clean firmware and crypto C6 builds PASS (0x2e390 / 0x3b810 bytes); root normal
  and ASan/UBSan suites 16/16 each, including vendor sanitizer instrumentation;
  simulator/history/controller/E2EE suites 4/4. No crypto warnings/regressions;
  existing duplicate-LVGL-library linker warning remains. git diff --check PASS.
- EMB-002 connection/reconnect sources/tests excluded and untouched; concurrent
  connection-source changes preserved. No protocol, pairing, persistent format,
  flash, commits or pushes. SDK/configuration/compiled-code evidence is recorded
  in docs/security/SEC-002.md and build/sec002/evidence.json.
- GO for C6 development baseline; NO-GO for permanent identities until physical
  entropy lifecycle and protected-key validation. S3/hardware and storage
  power-cut/GC tests remain pending. Updated relevant guides/status/architecture.

## 2026-11-02 — SEC-001 embedded crypto assessment: NO-GO

- Verified installed ESP-IDF 5.5.2 and Mbed TLS 3.6.5-11-gffb280bb63, all eleven
  Espressif patches, effective preprocessed C6 flags and linked experiment code.
  Confirmed CVE-2026-54435 applies to the X25519 public-derivation path; source
  and ancestry checks independently show the ECC correction is absent.
- Checked related ECC/PSA/entropy/compiler advisories for actual applicability.
  Confirmed official IDF 5.5.6 points to corrected Mbed TLS 3.6.7; SDK/toolchain
  upgrade is proposed, not installed or security-approved. No speculative
  submodule swap/backport, crypto-library alternative or provider change.
- Corrected entropy-source ownership in the isolated C6 crypto test only, using
  existing bootloader_support APIs before seed through cleanup. Added embedded
  public-derivation/low-order checks and host low-order/short-output regressions.
  Clarified provider entropy/reseed/cloning contract. No permanent real identity,
  protocol/format/pairing or falafel_reconnect implementation change.
- Normal and ASan/UBSan host suites 16/16 PASS each, including encrypted TCP/E2EE;
  C6 firmware and revised crypto experiment builds PASS (0x2d660 / 0x3a870).
  No flash/hardware execution or S3 build. git diff --check PASS.
- Added docs/security/SEC-001.md; updated README, firmware/crypto experiment
  guides, storage warning, architecture and project status. Analysis complete;
  remediation partial. Decision remains NO-GO for permanent identities pending
  corrected SDK validation, production entropy lifecycle and key protection.

## 2026-11-01 — ESP-IDF persistent storage backend

- Added separate falafel_storage and falafel_persistence firmware components.
  Dedicated NVS partition stores existing FMBX/FHI1/FHR1 bytes unchanged; full
  direction/ID mapping uses numbered slots, no truncated keys or extra data
  format. Exclusive ownership, explicit namespace creation, scan/orphan recovery,
  error poisoning and reopen; no auto erase/reset, history eviction or key store.
- Added isolated C6 storage defaults/partition table and existing-namespace boot
  probe without changing local sdkconfig or integrating GUI/network/pairing.
- Added actual-adapter host tests against a clearly labeled NVS API model with
  ephemeral in-RAM identities: 100 encrypted receive/reopen cycles, exact duplicate
  ACKs, 30 interrupted/error outcomes, corruption, init failures, ENOSPC and all
  1024 IDs. Model does not verify flash page recovery, power-cut, GC or wear.
- Verification: root normal and ASan/UBSan suites 16/16 each; C6 IDF 5.5.2 build
  succeeds (0x2d660-byte binary). Initial sandbox socket/process-list failures
  resolved by authorized reruns. No hardware flash/run or S3 build performed;
  S3 toolchain not installed. git diff --check passes.
- Decision: use built-in NVS for the bounded first archive; document LittleFS,
  SPIFFS/FAT/raw-journal tradeoffs, flash headroom, wear and physical acceptance.
  Embedded Mbed TLS limitation remains open: no real identities/data approved.
- Updated firmware guide, storage/architecture/persistence docs, README and
  project status; next step is corrected SDK plus disposable C6 power-cut tests.

## 2026-10-31 — Persistent encrypted history and safe mailbox slot reuse

- Added portable shared/storage/falafel_history.c and POSIX adapter with public
  headers: AES-256-GCM records/manifest, provider HKDF domain separation from local
  identity, random nonces, authenticated archive UUID, monotonic ID-based updates,
  manifest/orphan recovery and fail-closed corruption handling. No plaintext
  history or separate raw local AES-key file; fixture key protection is explicit.
- Extended mailbox/link with opt-in durable retirement callbacks, archived exact
  DATA/ACK lookup, authenticated late ACK checks, idempotent retired consume,
  fair bounded archived-ACK scheduling and safe active-slot reuse. Eight active
  slots/direction remain; current archive capacity is 1024 total IDs/device,
  with FULL/no eviction. Default unupgraded v1 behavior remains for legacy tests.
- Added explicit local FMBX v1 -> v2 migration (8376 -> 8392 bytes) with archive
  UUID binding. Missing/corrupt/wrong archives cannot be recreated for v2 even by
  --init-history. Network HELLO2/DATA/RECEIPT, Noise K provider and relay unchanged.
  Legacy unsaved outgoing text is marked unavailable; original ciphertext/IDs and
  ACK/status processing survive. No unsafe downgrade is offered.
- Controller now persists outgoing intents before enqueue, binds existing exact
  wire bytes after restart, saves incoming effects before consume, and archives
  before clear. CLI adds --history/--init-history. Local reads persist through
  worker commands; GUI upserts restore sent/received text and status by ID, without
  changing character, palette, screen/control positions or rotation assets.
- Fixed TCP input batching to parse a full 4096-byte batch instead of disconnecting
  on valid large replay. Updated CMake targets, probe and integration tests.
- Added test_history: 100 sequential messages each direction, 41 real process-
  death boundaries including migration and retirement, active/archive FULL,
  exact saved receipts, late ACKs, read state, ID conflicts, wrong keys/archive
  UUID and corruption. SDL tests send 100/direction and restore 200 rows/device,
  delivery/read states, relay offline/restart/replay and no duplicate rows.
- Final normal and ASan/UBSan simulator suites passed 4/4; root suites passed
  15/15 each. Builds, snapshots and git diff --check passed. Intermediate test
  assertions for separately generated receipts/pre-migration snapshots were
  corrected; no final test failures. Actual power-cut/hardware tests not run.
- Measured history context 55,520 bytes; controller/history 93,376; GUI cache
  49,152; 200-record history 163,460 disk bytes plus 8,392-byte mailbox. LVGL
  peak with 200 rows 3,067,008 bytes; loop gaps 24 ms normal/63 ms instrumented.
  Allocator/OS/crypto stacks/filesystem overhead extra; no ESP32-S3 budget claimed.
- Added docs/application-persistence.md; updated README, simulator guide,
  architecture, protocol runtime/local-format notes, E2EE design, project status,
  headless and relay documentation. Production secure keys/rotation/backup,
  anti-rollback, flash wear, larger relay retention/replay and ESP32-S3 adapters
  remain pending. Existing build warnings remain. No commit or push.

## 2026-10-31 — E2EE LVGL messaging integration increment

- Added simulator/src/messaging_controller.{c,h}: one pthread owns existing
  identity/crypto/mailbox/link/TCP operations; 8-command and 32-event SPSC atomic
  queues copy data to/from LVGL, whose loop handles at most four events per turn.
  No LVGL calls from worker, no legacy message fallback, no new protocol.
- Replaced simulator main's legacy event queue/state callbacks with strict v2
  controller dispatch and required --identity/--mailbox paths. Missing, corrupt,
  mismatched or locked stores fail closed; no automatic enrollment/reset. Existing
  explicit random fixture identities are supported through a trusted-file adapter.
  SDL close/quit, SIGINT and SIGTERM request worker shutdown while LVGL continues;
  accepted commands drain when storage works, and persistent outbox recovers.
  A separate terminal-failure flag preserves nonzero exit status even when
  shutdown releases a saturated event queue.
- Added cmake/simulator_messaging.cmake and updated simulator build/config to reuse
  the existing pinned Mbed TLS provider and mailbox runtime. v1 helper code remains
  a transport link dependency only. Crypto, relay implementation, protocols and
  mailbox format unchanged; supplied PNGs and rotation unchanged.
- Updated shared GUI to remove demo data, show empty/available mailbox content,
  send the 'Sto arrivando' preset, distinguish asynchronous send phases, and color
  the envelope for unread data. Composer callback semantics no longer claim
  immediate durable send. Personality changes are local/session-only.
- Incoming messages are projected once per retained slot/process without consume;
  available ciphertext reconstructs on restart. Outgoing rows/read state remain
  session-only and the view says so. Durable protected application history and
  idempotent effect-before-consume are explicitly pending, not implemented.
- Added test/messaging_probe.c and test/test_simulator_messaging.py, fixture and
  probe targets, and controller/SDL integration CTests. Normal and ASan/UBSan
  simulator suites passed 3/3 (rotation smoke, four controller cases, two SDL
  cases). Covered two independent simulator identities/mailboxes, bidirectional
  E2EE, recipient offline, sender restart, saved ACK with recipient offline,
  relay restart/live reconnect, no duplicate rows/events, window close, queue
  saturation/full-queue stop, bad inputs and runtime storage failure. Retained
  inbox state and absence of plaintext in relay/mailbox files verified.
- Measured maximum E2EE LVGL loop gaps: 24 ms normal, 71 ms instrumented. Existing
  root Debug suite passed 15/15. Snapshots inspected; git diff --check passed.
  Existing linker duplicate-library and upstream CMake deprecation warnings
  remain; hardware/physical trackpad, production enrollment/history unverified.
- Updated README, simulator guide, architecture, protocol runtime attribution,
  E2EE design, project status and headless/relay guides. No commit or push.

## 2026-10-31 — Prerendered personality rotation

- Modified shared/gui/falafel_home.c and falafel_gui_internal.h to play the 24
  supplied RGBA PNGs in order on accepted horizontal swipes, using a 30 ms LVGL
  timer (nominal 720 ms). Selection and callbacks commit after the last frame;
  repeated swipes are discarded, remote selection cancels pending local state,
  and the static PNG returns. No protocol or application-core changes.
- Extended tools/embed_simulator_asset.py and simulator/CMakeLists.txt to embed
  validated frame_000.png through frame_023.png as compressed const descriptors.
  Explicitly disabled decoded cache and set 15 ms refresh in config/lv_conf.h.
  Documented asynchronous selection semantics in shared/include/falafel_gui.h.
  Kept current circular sage layout, original PNGs, notifications and messages.
- Extended simulator/src/ui_smoke.c to observe all frame draws and their order,
  check busy rejection, deferred callbacks, message navigation during playback,
  notification timeout, static restore, remote cancellation and timer heartbeat.
- Normal and ASan/UBSan builds and SDL smoke passed. Normal playback measured
  715–739 ms; instrumented playback 840–1329 ms. Heartbeat maximum gaps were
  58 ms normal and 115 ms instrumented. Root Debug regressions passed 15/15.
  Snapshots visually inspected; git diff --check passed. Sanitizer test timing
  now waits for completion; real SDL input can interfere with automation.
- Rotation assets total 2,847,217 compressed bytes; one decoded frame needs
  868,624 bytes. Measured LVGL heap peak including snapshots is about 3.75 MiB,
  within the existing 8 MiB desktop budget. Hardware flash/PSRAM and PNG decode
  timing are unverified; synchronous per-draw decoding can extend playback.
- Updated README, simulator guide, architecture and project status. Existing
  duplicate-library linker warning remains. E2EE, mailbox, relay and protocols
  unchanged; no commit or push.

## 2026-10-31 — Sage circular simulator styling

- Modified only LVGL presentation in shared/gui and simulator/src/ui_smoke.c:
  dark sage #34483E, cream #F3E9D8, toasted-orange #D88A35 and flat sage surfaces.
- Reduced character stage from 350px to 320px, envelope button to 44 × 40 with
  an 18px icon above the head, personality text to 16px and position dots to 5px.
  Preserved the 466 × 466 circular mask and existing navigation/callbacks.
- Updated snapshot assertions for sage masked corners and relocated click;
  extended smoke coverage to notification banner display and automatic timeout.
- CMake/Ninja normal and ASan/UBSan simulator builds and SDL smoke tests passed.
  Root Debug regressions passed 15/15. Home/message/banner snapshots inspected;
  git diff --check passed. GUI/socket tests required execution outside the
  sandbox. Existing duplicate-library linker warning remains; physical trackpad
  and hardware remain untested.
- Updated README, simulator guide, architecture and project status. PNG asset,
  messaging logic, relay and E2EE core unchanged. No commit or push.

## 2026-10-30 — Initial circular Falafel simulator UI

- Changed the macOS SDL2 viewport to fixed 466 × 466 and clipped LVGL content
  to a circle over black, including the notification page.
- Added build-time embedding of the supplied `simulator/assets/falafel.png`,
  using LVGL 9.4 LodePNG and uniform scaling to a 350px stage. Same character
  for all personalities, with Taxi/Cuoco/Promemoria labels and position dots.
- Reused mouse/trackpad horizontal navigation. Added an always-available envelope
  icon, preset local message and return button; initial view omits rectangular
  status/composer chrome. Existing --device/--peer and legacy callbacks retained.
- Added actual SDL input smoke automation and LVGL snapshot checks for PNG
  rendering and circular clipping. CMake/Ninja build and GUI smoke passed;
  home/message snapshots visually inspected. Root Debug regressions passed
  16/16. git diff --check passed. Existing duplicate-library link warning remains.
- Raised desktop-only LVGL heap to 8 MiB for PNG/mask/snapshot buffers. No GUI
  sanitizer, hardware or physical trackpad testing claimed. E2EE, mailbox and
  relay code untouched; no commit or push.
- Updated simulator guide, README, architecture and project status. Next UI work
  remains notification/content refinement and physical input validation.

## 2026-10-30 — Milestone 3C.2C asynchronous encrypted TCP integration

- Added portable v2 frame codec/mailbox coordinator and headless desktop peer.
  Reuses mailbox exact-byte retry/deduplication/authenticated receipt logic and
  the existing TCP adapter rather than layering legacy application ACKs.
- Extended TCP adapter with a frame callback mode, HELLO2, bounded raw-frame send,
  progress/handshake deadlines and deterministic partial-I/O test support.
  Legacy simulator v1 path remains compatible and compiled successfully.
- Extended Python relay with durable opaque v2 data/receipt caching and offline
  redelivery, strict framing and optional --e2ee-only isolation. Relay sends no
  v2 ACKs, has no keys, and retains ciphertext/receipts without automatic eviction.
- Added a minimal idempotent sender/ID/content-hash consumer with durable effect
  before consume; crash recovery verifies the existing effect instead of appending.
  Demo credential provisioning is explicit and separate from production pairing.
- Debug/Release/ASan+UBSan root suites passed 16/16 each, including five subprocess
  E2E cases: offline A/B handoff with relay restart and saved-ACK recovery, reverse
  and duplicate receipts, fragmented mid-frame disconnect, invalid/unpaired
  payloads, false ACKs, full inbox/outbox/relay and consumer crash/restart. Relay
  files/logs checked for plaintext and private-key absence. Pairing/IK regressions
  passed 5/5 in each configuration. Final legacy/encrypted TCP tests repeated in
  all three builds; git diff --check passed. Initial sandbox socket denial was
  rerun with permission and is not counted as a pass.
- Updated protocol, async design, architecture, project status, relay/run docs and
  README. GUI/ESP32-S3, authenticated relay registration/TLS, production credential
  wiring and safe epoch-based compaction remain future work for 3C.3.

## 2026-10-30 — Milestone 3C.2B persistent mailbox and receipts

- Added portable `falafel_mailbox` state and independent 8,376-byte snapshot
  codec; ciphertext-only outbox/inbox, exact-byte retries, retained deduplication,
  durable consumption and eight lifetime records per direction. Full is explicit
  backpressure; no history eviction or plaintext/private-key persistence.
- Added POSIX desktop backend: exclusive lifetime lock, unique 0600 temporary,
  short-write handling, fsync/rename/directory fsync, strict recovery and
  poisoned-handle semantics after an uncertain save failure.
- Reused 3C.2A Noise K for domain-labelled data/ACK payloads. Receipts bind paired
  identities, message ID, durable-acceptance status and SHA256 of the exact data
  envelope. Saved ACK recovery is atomic with inbox acceptance. Legacy relay
  ACKs do not advance this isolated mailbox. No read receipts added.
- Defined at-least-once peek until persistent consume; external effects require
  idempotent consumer transactions. No exactly-once end-to-end claim.
- Debug/Release/ASan+UBSan root suites passed 15/15 each; existing pairing/IK
  regressions passed 5/5 each. Added 28 writer process-crash cases, consumer-ledger
  crash recovery, corruption, unavailable/full storage, failed writes, persistent
  retry/deduplication, forged/duplicate/out-of-order ACK tests. Repeated mailbox
  tests after final additions. `git diff --check` passed.
- Updated design, protocol, architecture, README and project status. Relay TCP,
  LVGL, Wi-Fi and ESP32-S3 integration remain outside this milestone.

## 2026-10-30 — Milestone 3C.2A offline cryptographic module

- Added portable `falafel_async_e2ee` C11 APIs and provider SHA-256 wrapper.
  Uses standard Noise K one-way handshake payloads with paired static identities,
  CSPRNG ephemeral keys, header prologue and a single nonce-zero AES-GCM operation
  per fresh handshake key. No custom DH concatenation construction was adopted.
- Added strict versioned binary envelopes (134-byte overhead, 122-byte plaintext,
  256-byte maximum), identity/size validation, temporary secret erasure and
  fail-closed output handling. No relay, inbox/outbox, ACK or LVGL integration.
- Added offline boundary/negative tests, per-provider-call fault injection, and
  optional bidirectional pinned Noise-C interoperability. Root Debug, Release,
  ASan/UBSan suites passed 14/14 each; pairing/persistence/IK regressions passed
  5/5 each in all three builds. Initial sandbox socket denials passed on rerun
  with localhost access. `git diff --check` passed.
- Accepted probabilistic uniqueness under a reliable CSPRNG, without hardware
  anti-rollback allocation. Documented replay acceptance, recipient compromise,
  KCI, visible metadata, lack of independent audit and untested ESP32-S3 support.
- Updated design, status, architecture, protocol and README to distinguish this
  implemented module from future runtime integration and 3C.2B persistence work.

## 2026-10-29 — 3C.2A design review

- Corrected the candidate design: CSPRNG-generated ephemeral keys and nonces
  give probabilistic collision resistance, not unconditional uniqueness across
  restart, cloned state or rollback. The current provider and pairing store do
  not supply a crash-safe, rollback-resistant allocator. No 3C.2A cryptographic
  implementation or offline delivery is claimed.

## 2026-10-29 — 3C.1 negative-test verification and 3C.2 design

- Strengthened the two-process wrong-identity test to verify that the closed
  session also rejects application payload decryption.
- Documented a candidate asynchronous envelope and its framing, authentication,
  crash recovery, replay, forward-secrecy and rollback limitations; the 3C.2
  implementation remains pending.

## 2026-10-28 — Milestone 3C.1 verification completion

- Extended the desktop process harness to send an IK handshake from a newly
  generated identity and verify rejection against the persisted paired peer.
- Tested the actual Noise cipher nonce and session sequence at `UINT64_MAX - 1`:
  the final valid record succeeds, nonce rewind is refused, and subsequent send
  or receive attempts close the session without nonce reuse. The boundary setup
  exists only in the test build and adds no production nonce-setting API.
- Debug, Release and ASan/UBSan pairing/E2EE suites: 5/5 passed each; existing
  repository regressions: 11/11 passed. `git diff --check` passed.

## 2026-10-28 — Milestone 3C.1 live E2EE prototype

- Added portable Noise IK session with paired static identity verification,
  directional AES-GCM states, authenticated sequence numbers and fail-closed
  session handling; desktop persistence harness now exchanges encrypted data
  between restarted processes twice.
- Debug, Release and ASan/UBSan desktop suites: 5/5 passed each; repository
  regressions: 11/11 passed. Tests cover wrong peer, altered ciphertext,
  replay, out-of-order sequence, reconnection and explicit close.
- This ordered live channel does not define offline store-and-forward E2EE.

## 2026-10-27 — Milestones 3B.1–3B.3 consolidation

### Components modified

- Shared pinned Noise-C CMake declaration and the three 3B build targets.
- Ignore rules and reproducible build/test documentation.

### Changes and verification

- Centralized revision `cfe25410979a87391bb9ac8d4d4bef64e9f268c6` and archive
  SHA-256 verification. Clean builds now fetch under their build directories;
  `NOISE_C_SOURCE_DIR` is only an optional offline override.
- Added explicit ignore patterns for pairing state and atomic temporary files.
- Removed literal private-key test fixtures from the feasibility harness; each
  side now generates its distinct test identity with Noise-C's platform RNG.
- Clean Noise Debug and ASan/UBSan suites: 1/1 passed in each.
- Noise-C upstream `make check` unit and vector suite passed from the fetched
  pinned source tree, including the selected XX/25519/AES-GCM/SHA-256 vector.
- Clean pairing Debug, Release, and ASan/UBSan suites: 4/4 passed in each,
  including Keychain restart/persistence and all negative tests.
- Existing normal and sanitizer regressions: 11/11 passed in each.
- Repository scan found no committed local state, private key, or credential.

## 2026-10-27 — Milestone 3B.3 persistent desktop pairing

### Components modified

- Portable persistent pairing state, validation, and metadata codec.
- macOS Keychain/atomic-file storage backend and restart harness.
- Pairing API peer-key export restricted to bilaterally completed sessions.
- Desktop pairing build, tests, architecture, status, protocol, and README.

### Features implemented and bugs fixed

- Local identities are created before pairing and reused across restarts;
  private keys remain in Keychain and are never written to metadata or logs.
- Verified peer keys are committed only from `COMPLETE`; rejected and
  incomplete sessions cannot export a persistable peer.
- Atomic `0600` metadata replacement uses random same-directory temporary
  files, file/directory `fsync()`, and rename. Corrupt state fails closed and
  explicit unpair supports identity rotation.
- Fault injection exposed and fixed temporary-name collision after an
  interrupted save by replacing PID-derived names with `mkstemp()`.

### Tests performed

- Fresh AppleClang Debug, Release, and ASan/UBSan builds: 4/4 CTest targets
  passed in each configuration.
- Covered codec corruption/truncation/key mismatch, interrupted replacement,
  process restart, peer recognition, incomplete pairing restart, unpair, and
  identity rotation using temporary real macOS Keychain items.
- Existing repository and Noise feasibility regressions: passed.

### Architectural decisions and limits

- Filesystem operations are outside the portable state module. Keychain holds
  the only at-rest copy of the local private key; the file is public metadata.
- CRC32 and atomicity detect corruption/torn writes but cannot prevent replay
  of an older valid metadata file. No anti-rollback guarantee is claimed.

## 2026-10-27 — Milestone 3B.2 desktop Noise XX pairing

### Components modified

- Portable `shared/pairing` API and implementation.
- Standalone macOS pairing build, interactive peer, two-process harness, and
  pairing tests.
- README, architecture, protocol and project-status documentation.

### Features implemented and bugs fixed

- Complete initiator/responder Noise XX exchange with distinct X25519
  identities using the Noise-C revision pinned by 3B.1.
- Domain-separated SHA-256 SAS derivation with 40-bit rejection sampling and a
  zero-padded 12-digit display.
- Explicit state enforcement plus transcript-, SAS-, and role-bound decisions
  authenticated with the Noise `Split()` cipher states; completion requires
  both accepts.
- The corrected ESP32-C6 benchmark completed 100 handshakes with PASS and no
  watchdog warnings.
- Fixed the 3B.2 Release test build by explicitly keeping assertions enabled
  for `test_pairing`; Release optimization and `-Werror` remain enabled, and
  no positive or negative check is compiled out by `NDEBUG`.

### Tests performed

- AppleClang debug build and CTest pairing unit/two-process suite: passed.
- Positive completion and negative SAS mismatch, rejection, altered handshake,
  altered confirmation, missing confirmation, premature confirmation,
  duplicate and terminal-session reuse cases: passed.
- ASan/UBSan pairing unit and two-process tests: 2/2 passed.
- Existing normal and ASan/UBSan repository suites: 11/11 passed in each build;
  the 3B.1 Noise feasibility regression also passed.
- Fresh Debug and Release 3B.2 builds: 2/2 CTest targets passed in each; the
  Release compile flags contain `-DNDEBUG` followed by `-UNDEBUG`, and the
  standalone negative-test executable passed with its assertions active.

### Architectural decisions

- Pairing is a portable module independent of GUI and transport; the desktop
  programs supply length-framed sockets only.
- No GUI, relay, event-v1, 3A provider, persistence, or application E2EE change
  is part of this milestone.

## 2026-10-26 — Milestone 3B.1 Noise feasibility gate

### Components modified

- Isolated host Noise-C experiment and selected-suite build.
- Standalone ESP32-C6 Noise experiment with platform RNG adapter and metrics.
- Project status and experiment documentation.

### Features implemented

- Complete `Noise_XX_25519_AESGCM_SHA256` exchange with distinct static keys,
  handshake-hash agreement, Split transport proof, bidirectional encryption,
  fresh ephemerals, and negative protocol/AEAD/state tests.
- Reproducible Noise-C commit and archive hash pinning without changing the 3A
  provider or application architecture.

### Tests performed

- AppleClang 21 release build: experiment passed; 100-handshake mean 204.22 us.
- AppleClang 21 ASan/UBSan build: passed.
- Noise-C upstream unit and vector suite: passed, including the independent
  Cacophony vector for the selected suite.
- ESP-IDF 5.5.2 / RISC-V GCC 14.2.0 cross-build: passed; binary `0x2af20`,
  total image content 175,796 bytes. Flash and physical execution not run
  because no serial device was visible.

### Architectural decisions

- Decision is NEED FURTHER EVALUATION, not adoption: the state machine is a
  viable candidate, while the stale, dynamically allocated reference backend
  duplicates the existing X25519, AES-GCM, and SHA-256 implementation.
- A provider/Mbed TLS backend experiment and physical C6 measurements are the
  gate before Milestone 3B.2. No custom Noise XX implementation was created.

## 2026-10-26 — ESP32-C6 crypto experiment stack fix

### Components modified

- ESP32-C6 crypto experiment entry point and sdkconfig defaults.
- ESP32-C6 experiment and project-status documentation.

### Bugs fixed

- Removed the 1,536-byte opaque crypto context from the 3,584-byte main-task
  stack and allocated it from internal 8-bit-capable heap instead.
- Preserved the configured main-task stack size rather than hiding the fault
  with an arbitrary stack increase.
- Unified self-test cleanup and final reporting so each boot produces exactly
  one clear `PASS` or `FAIL` result.

### Tests performed

- ESP32-C6 cross-build with ESP-IDF 5.5.2 and GNU RISC-V GCC 14.2.0.
- Disassembly check: 32-byte `app_main()` frame and 336-byte
  `known_vector_tests()` frame after the fix.
- Final physical ESP32-C6-DevKitC-1 run passed all cryptographic self-tests and
  returned normally from `app_main()` with Mbed TLS 3.6.5.
- Final measurements: 651,170 us elapsed; 3,584-byte configured main stack;
  1,952-byte minimum free stack; 460,768/460,296 bytes free heap before/after;
  434,176-byte largest free block; and `0x3a100`-byte application binary.
- All 11 desktop CTest targets normally and under ASan/UBSan, including crypto
  vectors, relay integration and transport end-to-end tests.

### Architectural decisions

- Long-lived opaque cryptographic contexts belong in explicitly managed heap
  storage on ESP-IDF; small fixed known-answer buffers remain local.
- FreeRTOS stack high-watermarks are logged around initialization, vector tests
  and cleanup so later growth is measurable.
- The C6 hardware self-test uses the NIST AES-256-GCM one-block vector because
  ESP-IDF 5.5.2's accelerated backend rejects zero-length input. The desktop
  suite retains coverage of the empty-plaintext vector.
- The shared crypto provider remains unchanged.

The stack correction and the subsequent one-block GCM self-test binary were
both confirmed on hardware. The 1,952-byte minimum free stack gives adequate
headroom for this bounded, one-shot experiment without increasing the task
stack. The 472-byte heap delta is recorded for future repeated-initialization
testing; it does not prevent the current one-shot program from completing.

The ESP-IDF dependency identifies itself as
`mbedtls-3.6.5-11-gffb280bb63`. Its local history does not contain the ECC
side-channel correction released in Mbed TLS 3.6.7, so this result validates
X25519 functionality but does not approve that dependency for production use
with persistent private keys.

## 2026-10-26 — Milestone 3A cryptographic foundation

### Components modified

- Shared portable crypto API and Mbed TLS provider.
- Root CMake, desktop tests and standalone ESP32-C6 experiment.
- Architecture, protocol, status and build documentation.

### Features implemented

- Secure RNG, X25519, HKDF-SHA256 and AES-256-GCM behind a C11 API without
  public Mbed TLS types.
- Constant-time equality, best-effort zeroization and explicit authentication
  failure handling.
- Official vectors, tamper/length/buffer tests and a C6 serial self-test with
  timing and heap measurements but no secret logging.

### Tests performed

- Host crypto and existing portable targets built with AppleClang.
- Crypto, core, protocol and persistence CTests passed.
- ESP32-C6 cross-build passed with ESP-IDF 5.5.2.
- All 11 CTest targets passed normally and under ASan/UBSan, including relay
  fault injection and transport end-to-end tests.

### Architectural decisions

- AES-256-GCM is used because C6/S3 support AES acceleration and ESP-IDF enables
  GCM without optional ChaCha/Poly1305 configuration.
- Desktop Mbed TLS is pinned to official 3.6.7 with archive hash verification.
- Runtime event v1, ACK semantics and GUI remain unchanged.

## 2026-10-26 — GCC CI warning fix

### Components modified

- Event protocol serialization formatting.

### Bugs fixed

- Removed two misleading-indentation diagnostics reported by GCC with
  `-Werror`, preserving the existing serializer and deserializer logic.

### Tests performed

- Complete 10-test host suite.
- Complete 10-test host suite with AddressSanitizer and
  UndefinedBehaviorSanitizer.
- Core and simulator builds with `-Werror -Wmisleading-indentation` under
  AppleClang.
- Full simulator build.

The local macOS environment does not provide GNU GCC or a Linux container, so
the final Ubuntu/GCC confirmation remains the next GitHub Actions run.

## 2026-10-26 — Reliability Hardening

### Components modified

- Portable application core and event validation.
- Desktop transport, argument handling and persistence.
- Python relay persistence and socket output handling.
- Simulator status integration.
- CMake, CI and automated tests.

### Features and fixes

- Made relay acceptance conditional on successful durable persistence.
- Added durable completed-delivery records and sender status recovery.
- Added explicit queue-full backpressure without eviction.
- Added persistent event-ID range reservation.
- Replaced raw C state dumps with versioned, fixed-width, checksummed encoding.
- Added application origin validation and idempotent acknowledgements.
- Added bounded durable received-message and deduplication state.
- Coalesced personality events only before first transport submission and
  preserved submitted or accepted events.
- Made DNS, connect, reads and writes non-blocking on desktop.
- Replaced relay `sendall()` with bounded selector-driven output buffers.
- Corrected the status bar to indicate relay connectivity rather than peer
  presence.

### Tests performed

- 10 CTest targets with AddressSanitizer and UndefinedBehaviorSanitizer.
- Protocol, application, persistence and malformed-state unit tests.
- Relay fault injection, restart, backpressure and fragmented I/O tests.
- C application-to-application integration through both desktop transports and
  the Python relay.
- Desktop simulator build and Python syntax validation.

### Architectural decisions

- Delivery is at-least-once with bounded application deduplication.
- The relay retains pending data until a durable recipient ACK transition.
- Relay-reported final status is accepted as recovery of a previously validated
  peer ACK, not as E2E authentication.
- Unsafe automatic migration from persistence v1 is rejected explicitly.
