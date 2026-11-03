# ESP32-C6 cryptographic experiment

This existing standalone ESP-IDF project compiles Falafel's actual Mbed TLS
provider. SEC-003 extends it with temporary RAM-only identities, known-answer
vectors, ECDH, authenticated encryption/decryption, tamper rejection, automatic
reseed observation, injected failures and cleanup checks. No production provider,
protocol, persistent identity or provisioning change is made.

**Software preparation verified; hardware validation NOT RUN. SEC-003 remains
OPEN. Permanent identities remain NO-GO on C6/S3.**

Use the isolated ESP-IDF 5.5.6 / Mbed TLS 3.6.7 baseline and the new safety
overlay. The definitive build, manual flash, UART monitor, five warm/five cold
reboot procedure and PASS/FAIL criteria are in
[SEC-003](../../docs/security/SEC-003.md). Build commands also appear in
[the firmware guide](../../firmware/README.md). Do not build current sources into
old SEC-001/002 directories or use an arbitrary active SDK.

The prepared artifact is
`build/sec003-20261103/crypto-c6-ready/falafel_esp32c6_crypto.bin` (248,208 bytes).
Revalidated from a fresh build on 2026-11-03; all earlier builds are retained.
The new configuration uses a 4,096-byte main-task stack and a heap-allocated
1,536-byte provider context. The experiment owns the internal SAR entropy source
through initialization and all reseeds, starts no RF/user ADC/sleep, and releases
ownership after cleanup. This policy is specific to the standalone experiment.

All 14 device cases must pass, with `SEC003 RESULT PASS passed=14 failed=0
cleanup=1`, adequate stack margin and actual hardware polling. This result has
**not** yet been observed on the board. Host rehearsal and raw provider regression
pass, including ASan/UBSan; native results are not hardware evidence. Public boot
fingerprints detect gross repetition only, never prove entropy quality.

Flash is configured to stay in the bootloader. Start the UART monitor without
reset, then press Reset to capture a complete run after the 10-second grace
period. No flash or monitor command was executed during software preparation.

## Historical evidence

An earlier ESP-IDF 5.5.2 physical run completed the older self-tests once:
651,170 us elapsed, 1,952 bytes minimum free stack from 3,584 bytes,
460,768/460,296 bytes free heap before/after cleanup, largest block 434,176 bytes,
and binary 0x3a100 bytes. It predates SEC-001 and SEC-003 and cannot validate the
current binary, entropy ownership, error paths or reboot behavior.

[SEC-001](../../docs/security/SEC-001.md) identified the old ECC vulnerability;
[SEC-002](../../docs/security/SEC-002.md) verified its correction in the isolated
5.5.6 source and linked code (historical binary 0x3b810). All previous SDKs,
configurations and artifacts remain unchanged. A newer SDK or functional PASS
alone does not establish security for permanent identities.
