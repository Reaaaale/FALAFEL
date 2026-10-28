# Noise feasibility experiment

This isolated Milestone 3B.1 experiment evaluates
`Noise_XX_25519_AESGCM_SHA256` with Noise-C. It does not integrate pairing,
SAS, persistence, the relay, or the application protocol.

## Reproducible dependency

- Repository: `https://github.com/rweather/noise-c`
- Commit: `cfe25410979a87391bb9ac8d4d4bef64e9f268c6` (2023-12-06)
- Upstream package version: `0.0.1` (no repository tags)
- Archive SHA-256: `d95cdd666124e9d1175c0c24024d7cabcd71149a9dbfe0de3aca43c562f7ac12`
- License: MIT

CMake downloads and verifies that exact archive into the selected build
directory unless `NOISE_C_SOURCE_DIR` names an already extracted copy. The
shared declaration is `cmake/noise_c_pinned.cmake` and is used by the desktop
feasibility, desktop pairing, and ESP32-C6 targets. No `/tmp` checkout is
required. The experiment compiles
only the selected suite. Link stubs make unavailable algorithms fail closed;
they are not a new Noise implementation. `noise_platform_random.c` replaces
Noise-C's POSIX/Windows random adapter with `getentropy()` on macOS and
`esp_fill_random()` on ESP-IDF.

Noise-C's reference backend duplicates AES-GCM, SHA-256, and Curve25519 rather
than using Falafel's Mbed TLS provider. This is acceptable only for the gate;
it is a central reason not to adopt the dependency unchanged.

## Desktop

```sh
cmake -S experiments/noise-feasibility -B build/noise-feasibility \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/noise-feasibility
ctest --test-dir build/noise-feasibility --output-on-failure
```

For ASan and UBSan add `-DFALAFEL_ENABLE_SANITIZERS=ON` in a separate build
directory. The executable performs 100 measured handshakes after correctness
tests. The timing covers construction, three handshake messages, hash checks,
and `Split()`, followed by complete cleanup; it does not include transport
encryption or process startup.

Covered positive cases include distinct static identities, all three XX
messages, equal handshake hashes, complementary Split states proven by
bidirectional encryption/decryption, fresh ephemeral output, and transport
encryption. Negative cases include truncated messages 1/2/3, alteration of
messages 1/2/3 (message 1 fails at message 2 because XX's first flight is not
authenticated), a 65,536-byte input, all-zero/low-order message-1 ephemeral,
wrong message order, altered AEAD tag, wrong transport key, and handshake-state
reuse.

The upstream `make check` suite also exercises the exact protocol against the
bundled Cacophony vectors, which originate from an independent Go
implementation. A same-library round trip is therefore not the sole
conformance evidence.

## ESP32-C6

With ESP-IDF exported, CMake downloads and verifies the same pinned archive in
the ESP-IDF build directory:

```sh
. "$HOME/esp/esp-idf/export.sh"
idf.py -C experiments/esp32c6-noise set-target esp32c6
idf.py -C experiments/esp32c6-noise build
idf.py -C experiments/esp32c6-noise -p /dev/cu.usbmodemXXXX flash monitor
```

For an offline build, `-DNOISE_C_SOURCE_DIR=/absolute/path/to/extracted/source`
may be passed explicitly; the default never relies on a fixed local path.

A successful physical run prints `esp32c6_noise_result: PASS`, timing, heap,
largest-free-block, and minimum-free-stack measurements. The 8 KiB main-task
stack is deliberate for the experiment and must be revisited after measuring
the physical run. No irreversible security settings are changed.

## Result and limits

Desktop functional, negative, sanitizer, upstream unit, and independent-vector
tests pass. ESP-IDF 5.5.2 cross-build passes. A later physical ESP32-C6 run of
the corrected benchmark completed 100 handshakes with PASS and no watchdog
warning.

Noise-C allocates handshake, symmetric, cipher, hash, DH, prologue, and random
states dynamically with `calloc()`/`malloc()` and wipes objects in
`noise_free()` before `free()`. Allocation failure paths exist, but the API has
no allocator injection. Exact peak heap and stack remain hardware-run data.

The 3B.1 recommendation was **NEED FURTHER EVALUATION**: retain the mature
Noise-C state machine as the leading candidate, but do not integrate its
reference crypto unchanged. Milestone 3B.2 subsequently uses it for an
isolated desktop pairing prototype; a narrowly scoped Mbed TLS/provider
backend and renewed vector coverage remain necessary before runtime adoption.
