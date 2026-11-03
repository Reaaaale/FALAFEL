# ESP-IDF storage experiment

Firmware probes mailbox/history storage. Wi-Fi, worker, enrollment, GUI and
drivers remain unintegrated. Boot creates no namespace, mailbox or identity.

**Development baseline: ESP-IDF 5.5.6 / Mbed TLS 3.6.7, validated on C6 by
[SEC-002](../docs/security/SEC-002.md). Permanent real identities: NO-GO.**
The ECC correction is present in compiled/linked code; physical entropy and key
protection remain unverified. SDK 5.5.2 and its existing builds are retained.

## Isolated installation

These are the executed installation steps, from the repository root. Do not
clone over an existing directory or delete old installations to repeat them.

```sh
git clone --branch v5.5.6 --depth 1 --recurse-submodules --shallow-submodules \
  --jobs 6 https://github.com/espressif/esp-idf.git .cache/sec002/esp-idf-v5.5.6
env -u IDF_PYTHON_ENV_PATH \
  IDF_TOOLS_PATH="$PWD/.cache/sec002/tools" \
  PIP_CACHE_DIR="$PWD/.cache/sec002/pip-cache" \
  VIRTUALENV_APP_DATA="$PWD/.cache/sec002/virtualenv-data" \
  bash .cache/sec002/esp-idf-v5.5.6/install.sh esp32c6
git -C .cache/sec002/esp-idf-v5.5.6/components/mbedtls/mbedtls \
  fetch --unshallow origin mbedtls-3.6.7-idf
```

The final fetch supplies ancestry evidence without changing the pinned checkout.
Tools, venv and caches stay under `.cache/sec002/`. No shell profile is modified.
Native CMake/Ninja/compiler/SDL remain system tools. Run commands with
`bash tools/with-idf-5.5.6.sh ...`; it checks SDK/Mbed TLS commits and activates
only its child process. Do not source the wrapper. With no arguments it reports
the version. The installed Python is 3.14.6 in the local idf5.5_py3.14_env venv.

## Compile ESP32-C6

```sh
bash tools/with-idf-5.5.6.sh idf.py -C firmware \
  -B "$PWD/build/sec002/firmware-c6" -DIDF_TARGET=esp32c6 \
  -DSDKCONFIG="$PWD/build/sec002/firmware-c6/sdkconfig" \
  -DSDKCONFIG_DEFAULTS="$PWD/firmware/sdkconfig.storage.defaults" build

bash tools/with-idf-5.5.6.sh idf.py -C experiments/esp32c6-crypto \
  -B "$PWD/build/sec003-20261103/crypto-c6-ready" -DIDF_TARGET=esp32c6 \
  -DSDKCONFIG="$PWD/build/sec003-20261103/crypto-c6-ready/sdkconfig" \
  -DSDKCONFIG_DEFAULTS="$PWD/experiments/esp32c6-crypto/sdkconfig.defaults;$PWD/experiments/esp32c6-crypto/sdkconfig.sec003.defaults" build
```

These build directories were initially empty. For another clean build use new
build/SDKCONFIG paths rather than deleting old ones. Existing firmware/experiment
configs remain unchanged. Firmware uses the 2 MiB storage-only layout, no GUI or
OTA. S3 needs a separate toolchain/build and is not validated here. No flash.

SEC-003 software preparation reuses the crypto experiment with its own safety
overlay and new build/SDKCONFIG. Its clean C6 build and native normal/sanitized
checks pass; **physical execution is NOT RUN and SEC-003 remains OPEN**. The
prepared binary is `build/sec003-20261103/crypto-c6-ready/falafel_esp32c6_crypto.bin`.
Use [SEC-003](../docs/security/SEC-003.md) for the exact manual flash/UART capture
and reboot protocol. It intentionally stays in the bootloader after flash and
uses `monitor --no-reset`; no hardware operation was performed during preparation.
Do not rebuild current crypto experiment sources in retained SEC-002 directories.
Permanent identities remain NO-GO. The host results below are the historical
SEC-002 full-project regression; SEC-003 adds focused native suites in its report.

## Host tests

The SDK source override tests the actual Espressif fork rather than reusing the
old host FetchContent archive. Test Python is explicitly isolated as well.

```sh
bash tools/with-idf-5.5.6.sh cmake -S . -B build/sec002/host -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_MBEDTLS="$PWD/.cache/sec002/esp-idf-v5.5.6/components/mbedtls/mbedtls" \
  -DPython3_EXECUTABLE="$PWD/.cache/sec002/tools/python_env/idf5.5_py3.14_env/bin/python" \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build/sec002/host -j4
ctest --test-dir build/sec002/host --output-on-failure

bash tools/with-idf-5.5.6.sh cmake -S . -B build/sec002/host-san -G Ninja \
  -DFETCHCONTENT_SOURCE_DIR_MBEDTLS="$PWD/.cache/sec002/esp-idf-v5.5.6/components/mbedtls/mbedtls" \
  -DPython3_EXECUTABLE="$PWD/.cache/sec002/tools/python_env/idf5.5_py3.14_env/bin/python" \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DFALAFEL_ENABLE_SANITIZERS=ON \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/sec002/host-san -j4
ctest --test-dir build/sec002/host-san --output-on-failure
```

Both suites passed 16/16, including vendor sanitizer instrumentation. Native
host defaults do not execute ESP hardware crypto/RNG. TCP tests need local
socket permission. Simulator suites passed 4/4 with the same Mbed TLS override
and isolated Python; see [SEC-002](../docs/security/SEC-002.md). EMB-002 is excluded.

After all recorded builds and simulator tests, regenerate read-only evidence:

```sh
bash tools/with-idf-5.5.6.sh python tools/verify-sec002.py > build/sec002/evidence.json
```

This verifies software provenance, not permanent-key safety or hardware power
cuts. See [storage design](../docs/embedded-storage.md) and
[SEC-001 closure requirements](../docs/security/SEC-001.md).
