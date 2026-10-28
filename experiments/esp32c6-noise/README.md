# ESP32-C6 Noise-C benchmark

This ESP-IDF target runs the Milestone 3B.1 Noise-C correctness suite and 100
`Noise_XX_25519_AESGCM_SHA256` handshakes. The benchmark yields to FreeRTOS for
10 ms between measured handshakes so the IDLE task can service the enabled task
watchdog; the delay is excluded from `average_handshake_us`.

The verified hardware baseline completed 100/100 handshakes with a 271199 us
average, 463776 bytes of initial heap, 463560 bytes of final heap, and 1784
bytes of minimum remaining stack, reporting `esp32c6_noise_result: PASS`.

Build, flash, and monitor after exporting ESP-IDF. The build uses
`cmake/noise_c_pinned.cmake` to download and SHA-256 verify the pinned Noise-C
archive inside its build directory; no external `/tmp` source tree is needed.

```sh
idf.py -C experiments/esp32c6-noise build
idf.py -C experiments/esp32c6-noise -p /dev/cu.usbmodemXXXX flash monitor
```

An explicit `-DNOISE_C_SOURCE_DIR=/absolute/extracted/path` remains available
for offline builds.
