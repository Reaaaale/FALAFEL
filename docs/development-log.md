# Development log

## Milestone 2 — Reliability Hardening (2026-10-26)

- Hardened the MVP to at-least-once delivery with durable relay acceptance,
  pending/completed recovery, persistent ID reservations and bounded replay
  protection.
- Replaced blocking connection and relay writes with bounded non-blocking state
  machines.
- Added a portable explicit desktop state format with CRC and strict rejection
  of corrupt or unsupported data.
- Added fault-injection and application-to-application transport integration
  tests. See `CHANGELOG.md` for the detailed session record.

## Milestone 3 — Local end-to-end MVP

- Added a portable fixed-capacity application queue, explicit delivery states,
  text validation, retry, ACK handling, and shared-personality convergence.
- Added a versioned routed event envelope while preserving the legacy protocol.
- Connected LVGL to a non-blocking desktop TCP adapter and local persistence.
- Added a standard-library Python relay with pair authorization, deduplication,
  offline persistence, reconnection, and distinct relay/device ACKs.
- Added core, persistence, malformed-event, conflict, offline/restart, and relay
  integration coverage. Hardware and production security remain out of scope.

## Milestone 2 — Complete simulated GUI

- Added a three-personality home (Taxi, Cuoco, Promemoria) navigated by a
  horizontal gesture, with dedicated primitive artwork and page indicators.
- Added reusable status bar setters for Wi-Fi, peer connection, battery, and
  unread message count.
- Added a scrollable sent/received conversation and a working LVGL textarea and
  virtual keyboard composer.
- Added temporary in-app notifications which preserve the active screen.
- Introduced platform-independent callbacks for send, personality change, and
  message-list opening. Simulator callbacks log events; no network request is
  made by the GUI.
- Split the shared GUI into coordinator, theme, status, home, messages, and
  composer modules. Shared GUI code includes only LVGL and standard C headers.

## Milestone 1 — Desktop Simulator

- Verified the local macOS build tools and SDL2 installation.
- Added a pinned LVGL dependency resolved by CMake into the build directory.
- Added a 320 x 480 SDL2 host with a minimal shared LVGL screen.
- Added a minimal ESP-IDF project skeleton without board drivers or pin guesses.
- ESP-IDF was not found in the current shell (`idf.py` absent and `IDF_PATH`
  unset), so firmware compilation is deferred until that environment is
  installed or activated.
