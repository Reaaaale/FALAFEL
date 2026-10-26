# ESP32-C6 cryptographic experiment

This standalone ESP-IDF application compiles the shared Falafel crypto provider
and runs functional tests on real hardware. It is not the Falafel firmware and
does not change the main firmware target.

```sh
idf.py -C experiments/esp32c6-crypto set-target esp32c6
idf.py -C experiments/esp32c6-crypto build
idf.py -C experiments/esp32c6-crypto -p /dev/cu.usbmodemXXXX flash monitor
```

Exit the monitor with `Ctrl-]`. The program reports pass/fail, elapsed time,
free heap, largest free block and the minimum main-task stack space observed at
startup, after crypto initialization, after the known-vector tests and after
cleanup. It runs the self-tests once and never logs keys or shared secrets.

The experiment deliberately keeps the ESP-IDF main-task stack at its 3,584-byte
default. The opaque 1,536-byte crypto context is allocated from internal
8-bit-capable heap instead of the main-task stack. This avoids the stack
protection fault seen when that context was a local `app_main()` variable,
without masking the problem by increasing the task stack. Allocation failure
produces a clear final `FAIL` result.

A successful hardware run ends with:

```text
cryptographic self-tests: PASS
```

All reported stack watermarks must remain greater than zero. Record the lowest
value when validating a new ESP-IDF release or target; a small margin should be
investigated before adding more work to `app_main()`.

The final ESP32-C6-DevKitC-1 run with ESP-IDF 5.5.2 completed all self-tests
once and returned normally from `app_main()`. It measured 651,170 us elapsed,
1,952 bytes minimum free from the 3,584-byte main-task stack, 460,768/460,296
bytes free heap before/after cleanup, a 434,176-byte largest free block and a
`0x3a100`-byte application binary.

The AES-256-GCM hardware known-answer test uses the NIST one-block vector. The
ESP-IDF 5.5.2 accelerated GCM path rejects a zero-length input with
`esp-aes-gcm: No input supplied`, so the empty-plaintext vector used by the
desktop backend is not suitable for this hardware experiment. Both vectors are
standards-based; this difference changes only the self-test input, not the
shared provider.

ESP-IDF 5.5.2 contains `mbedtls-3.6.5-11-gffb280bb63`. Inspection of that
bundled repository shows that the 11 Espressif commits after the 3.6.5 tag do
not contain the ECC side-channel correction released in Mbed TLS 3.6.7. It is
suitable for this functionality test, but production integration with
persistent X25519 keys must use an Espressif release verified to contain the
correction and repeat these tests on the selected hardware.
