#!/usr/bin/env python3
"""Offline SEC-003 evidence only: no flash, serial access or hardware approval."""
import argparse
import hashlib
import importlib.util
import json
import pathlib
import struct

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sec002_checks', ROOT / 'tools/verify-sec002.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
base.BUILD = ROOT / 'build/sec003'
BUILD = base.BUILD / 'crypto-c6-ready'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-root', type=pathlib.Path, default=base.BUILD,
                        help='isolated build root containing crypto-c6-ready, host and host-san')
    args = parser.parse_args()
    global BUILD
    base.BUILD = args.build_root.resolve()
    BUILD = base.BUILD / 'crypto-c6-ready'
    base.require(base.run('git', '-C', base.SDK, 'rev-parse', 'HEAD') ==
                 '880a203ae603d08a94d06aa01e0d6761958e5e9e', 'SDK pin differs')
    base.require(base.run('git', '-C', base.MBED, 'rev-parse', 'HEAD') ==
                 '2b96dd8eebe880f304c69976b3c2fa0c5100cbb6', 'Mbed TLS pin differs')
    base.require(not base.run('git', '-C', base.SDK, 'status', '--porcelain', '--untracked-files=no'), 'SDK modified')
    digest = hashlib.sha256((base.MBED / 'library/ecp_curves.c').read_bytes()).hexdigest()
    base.require(digest == 'db2d2c08fd5b7a28d9a082cdce1af6c266bafd5dfbdee0f162183a07fa0bfe86', 'ECC source differs')
    evidence = {'build_root': str(base.BUILD),
                'embedded': base.embedded('crypto-c6-ready'), 'ecp_curves_sha256': digest}
    commands = json.loads((BUILD / 'compile_commands.json').read_text())
    for name, interposed in [('falafel_crypto_mbedtls.c', True), ('sec003_probe.c', False)]:
        entry = next(e for e in commands if pathlib.Path(e['file']).name == name)
        base.require(('-Dmbedtls_entropy_func=sec003_entropy_func' in entry['command']) == interposed,
                     'callback probe compile scope differs')
    config = (BUILD / 'sdkconfig').read_text()
    for setting in ['CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT=y', 'CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y',
                    'CONFIG_ESPTOOLPY_AFTER_NORESET=y', 'CONFIG_ESP_CONSOLE_UART_DEFAULT=y',
                    'CONFIG_ESP_MAIN_TASK_STACK_SIZE=4096', 'CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200']:
        base.require(setting in config, 'missing experiment safety setting ' + setting)
    for setting in ['CONFIG_SECURE_BOOT', 'CONFIG_SECURE_FLASH_ENC_ENABLED', 'CONFIG_PM_ENABLE']:
        base.require(f'# {setting} is not set' in config, 'unexpected enabled setting ' + setting)
    flashing = json.loads((BUILD / 'flasher_args.json').read_text())
    base.require(flashing['extra_esptool_args']['after'] == 'no_reset', 'manual flash must stay in bootloader')
    prefix = evidence['embedded']['compiler'].removesuffix('gcc')
    elf = BUILD / 'falafel_esp32c6_crypto.elf'
    image = BUILD / 'falafel_esp32c6_crypto.bin'
    elf_digest = hashlib.sha256(elf.read_bytes()).hexdigest()
    binary = image.read_bytes()
    # Pinned ESP-IDF image: first-segment app descriptor at 0x20, hash offset 144.
    base.require(struct.unpack_from('<I', binary, 0x20)[0] == 0xabcd5432, 'unexpected image descriptor')
    base.require(binary[176:208].hex() == elf_digest, 'serial ELF hash would mismatch')
    symbols = base.run(prefix + 'nm', elf)
    for symbol in ['sec003_entropy_func', '__wrap_mbedtls_hardware_poll', 'mbedtls_hardware_poll',
                   'mbedtls_ecp_mod_p255_raw', 'bootloader_random_enable', 'bootloader_random_disable',
                   'falafel_crypto_init', 'falafel_crypto_random', 'falafel_crypto_x25519_generate',
                   'falafel_crypto_x25519_shared', 'falafel_crypto_hkdf_sha256',
                   'falafel_crypto_aead_encrypt', 'falafel_crypto_aead_decrypt']:
        base.require(any(x.endswith(' ' + symbol) for x in symbols.splitlines()), 'missing symbol ' + symbol)
    disassembly = {}
    for function, expected in [('falafel_crypto_init', 'sec003_entropy_func'),
                               ('mbedtls_entropy_init', '__wrap_mbedtls_hardware_poll'),
                               ('__wrap_mbedtls_hardware_poll', 'mbedtls_hardware_poll'),
                               ('sec003_entropy_func', 'mbedtls_entropy_func')]:
        text = base.run(prefix + 'objdump', '-d', '--disassemble=' + function, elf)
        base.require(expected in text, 'missing linked callback path in ' + function)
        disassembly[function] = text
    evidence.update({'elf_sha256': elf_digest, 'bin_sha256': hashlib.sha256(binary).hexdigest(),
                     'callback_disassembly': disassembly, 'native_suites': {}})
    for directory in ['host', 'host-san']:
        build = base.BUILD / directory
        log = (build / 'Testing/Temporary/LastTest.log').read_text()
        base.require(log.count('Test Passed.') == 3 and 'Test Failed.' not in log and 'End testing:' in log,
                     'native tests missing/failed')
        entries = json.loads((build / 'compile_commands.json').read_text())
        providers = [e for e in entries if pathlib.Path(e['file']).name == 'falafel_crypto_mbedtls.c']
        base.require(len(providers) == 2 and sum('mbedtls_entropy_func=sec003_entropy_func' in e['command']
                                               for e in providers) == 1, 'raw/probed native provider scope differs')
        curves = next(e for e in entries if pathlib.Path(e['file']).resolve() == base.MBED / 'library/ecp_curves.c')
        if directory == 'host-san':
            base.require('-fsanitize=address,undefined' in curves['command'], 'vendor not sanitized')
        evidence['native_suites'][directory] = {'passed': 3, 'python': base.python_environment(build, 'Python3_EXECUTABLE')}
    evidence['software_preparation'] = 'PASS'
    evidence['hardware_execution'] = 'NOT_RUN'
    evidence['SEC003'] = 'OPEN_PENDING_PHYSICAL_TESTS'
    evidence['permanent_identities'] = 'NO_GO'
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
