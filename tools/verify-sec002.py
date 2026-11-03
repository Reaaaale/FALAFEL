#!/usr/bin/env python3
"""Read-only SEC-002 build/provenance checks; never security approval for keys."""
import hashlib
import json
import pathlib
import shlex
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
SDK = ROOT / '.cache/sec002/esp-idf-v5.5.6'
TOOLS = ROOT / '.cache/sec002/tools'
MBED = SDK / 'components/mbedtls/mbedtls'
BUILD = ROOT / 'build/sec002'


def run(*args, cwd=ROOT):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=True).strip()


def require(condition, message):
    if not condition:
        raise SystemExit('SEC-002 verification failed: ' + message)


def python_environment(build, key):
    line = next(x for x in (build / 'CMakeCache.txt').read_text().splitlines()
                if x.startswith(key + ':'))
    executable = line.split('=', 1)[1]
    prefix = pathlib.Path(run(executable, '-c', 'import sys; print(sys.prefix)'))
    require(prefix.is_relative_to(TOOLS / 'python_env'), 'Python escaped isolated venv')
    return {'executable': executable, 'prefix': str(prefix)}


def embedded(directory):
    build = BUILD / directory
    description = json.loads((build / 'project_description.json').read_text())
    require(pathlib.Path(description['idf_path']).resolve() == SDK, 'wrong SDK path')
    require(description['target'] == 'esp32c6', 'wrong embedded target')
    require(pathlib.Path(description['config_file']).resolve().is_relative_to(build),
            'SDKCONFIG is not isolated')
    entries = json.loads((build / 'compile_commands.json').read_text())
    provider = next(e for e in entries if pathlib.Path(e['file']).name == 'falafel_crypto_mbedtls.c')
    curves = [e for e in entries if pathlib.Path(e['file']).name == 'ecp_curves.c']
    require(any(pathlib.Path(e['file']).resolve() == MBED / 'library/ecp_curves.c'
                for e in curves), 'corrected source not compiled')
    args = shlex.split(provider['command'])
    compiler = pathlib.Path(args[0]).resolve()
    require(compiler.is_relative_to(TOOLS), 'compiler escaped isolated tools')
    filtered = []
    skip = False
    for arg in args:
        if skip:
            skip = False
        elif arg == '-o':
            skip = True
        elif arg not in ('-c', provider['file']):
            filtered.append(arg)
    output = run(*filtered, '-E', '-dM', provider['file'], cwd=provider['directory'])
    macros = {}
    for line in output.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) >= 2:
            macros[fields[1]] = fields[2] if len(fields) == 3 else ''
    require(macros.get('MBEDTLS_VERSION_NUMBER') == '0x03060700', 'wrong Mbed TLS headers')
    selected = ['MBEDTLS_ECP_C', 'MBEDTLS_ECDH_C', 'MBEDTLS_ECP_DP_CURVE25519_ENABLED',
                'MBEDTLS_ENTROPY_HARDWARE_ALT', 'MBEDTLS_CTR_DRBG_C', 'MBEDTLS_GCM_C',
                'MBEDTLS_AES_C', 'MBEDTLS_HKDF_C']
    require(all(name in macros for name in selected), 'required primitive disabled')
    selected += ['MBEDTLS_VERSION_NUMBER', 'MBEDTLS_VERSION_STRING',
                 'MBEDTLS_ECP_MUL_ALT_SOFT_FALLBACK', 'MBEDTLS_ECDH_VARIANT_EVEREST_ENABLED',
                 'MBEDTLS_USE_PSA_CRYPTO', 'MBEDTLS_CTR_DRBG_ENTROPY_LEN',
                 'MBEDTLS_CTR_DRBG_KEYSIZE', 'MBEDTLS_CTR_DRBG_RESEED_INTERVAL',
                 'MBEDTLS_AES_ALT', 'MBEDTLS_GCM_ALT', 'MBEDTLS_NO_PLATFORM_ENTROPY',
                 'CONFIG_SECURE_BOOT', 'CONFIG_SECURE_FLASH_ENC_ENABLED']
    return {'sdk': description['idf_path'], 'compiler': str(compiler),
            'python': python_environment(build, 'PYTHON'),
            'compiler_version': run(compiler, '--version').splitlines()[0],
            'macros': {name: macros.get(name) for name in selected},
            'binaries': {p.name: p.stat().st_size for p in build.glob('*.bin')}}


def main():
    require(run('git', '-C', SDK, 'rev-parse', 'HEAD') ==
            '880a203ae603d08a94d06aa01e0d6761958e5e9e', 'wrong SDK commit')
    require(run('git', '-C', MBED, 'rev-parse', 'HEAD') ==
            '2b96dd8eebe880f304c69976b3c2fa0c5100cbb6', 'wrong Mbed TLS commit')
    require(not run('git', '-C', SDK, 'status', '--porcelain', '--untracked-files=no'),
            'modified SDK or submodule')
    fixes = ['6b4b14ae719c4f46ea70600d63b0d50dc727ff3a',
             '05ce3e90eb863b751c4464633a94239fd1938608']
    for fix in fixes:
        subprocess.run(['git', '-C', str(MBED), 'merge-base', '--is-ancestor', fix, 'HEAD'], check=True)
    digest = hashlib.sha256((MBED / 'library/ecp_curves.c').read_bytes()).hexdigest()
    require(digest == 'db2d2c08fd5b7a28d9a082cdce1af6c266bafd5dfbdee0f162183a07fa0bfe86',
            'reviewed fixed Curve25519 source differs')
    evidence = {'sdk_commit': run('git', '-C', SDK, 'rev-parse', 'HEAD'),
                'mbedtls_commit': run('git', '-C', MBED, 'rev-parse', 'HEAD'),
                'fix_ancestors': fixes, 'ecp_curves_sha256': digest,
                'firmware': embedded('firmware-c6'), 'crypto': embedded('crypto-c6')}
    compiler = pathlib.Path(evidence['crypto']['compiler'])
    elf = BUILD / 'crypto-c6/falafel_esp32c6_crypto.elf'
    prefix = str(compiler).removesuffix('gcc')
    symbols = run(prefix + 'nm', elf)
    for symbol in ['mbedtls_ecp_mod_p255_raw', 'mbedtls_ecp_keypair_calc_public',
                   'falafel_crypto_x25519_public_from_private']:
        require(any(line.endswith(' ' + symbol) for line in symbols.splitlines()),
                'missing linked symbol ' + symbol)
    evidence['p255_disassembly'] = run(prefix + 'objdump', '-d', '--disassemble=ecp_mod_p255', elf)
    require('mbedtls_ecp_mod_p255_raw' in evidence['p255_disassembly'],
            'linked Curve25519 reduction path differs')
    evidence['host_tests'] = {}
    for directory in ['host', 'host-san', 'simulator']:
        build = BUILD / directory
        entries = json.loads((build / 'compile_commands.json').read_text())
        curves = next(e for e in entries if pathlib.Path(e['file']).resolve() == MBED / 'library/ecp_curves.c')
        if directory == 'host-san':
            require('-fsanitize=address,undefined' in curves['command'], 'vendor source not sanitized')
        log = (build / 'Testing/Temporary/LastTest.log').read_text()
        require('Test Failed.' not in log and 'End testing:' in log, 'tests incomplete/failed')
        evidence['host_tests'][directory] = {'passed': log.count('Test Passed.'),
                                            'python': python_environment(build, 'Python3_EXECUTABLE'),
                                            'mbedtls_source': curves['file']}
    evidence['permanent_identity_decision'] = 'NO-GO: device entropy and key protection unverified'
    print(json.dumps(evidence, indent=2))


if __name__ == '__main__':
    main()
