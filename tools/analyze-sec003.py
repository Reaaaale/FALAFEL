#!/usr/bin/env python3
"""Read saved logs only. Never opens a serial device or approves permanent keys."""
import argparse
import hashlib
import json
import pathlib
import re

CASES = {
    'init_failure', 'init', 'rng_sanity', 'automatic_reseed',
    'reseed_failure_recovery', 'hkdf_kat', 'gcm_kat', 'x25519_rfc',
    'x25519_ephemeral', 'gcm_roundtrip', 'gcm_tamper', 'invalid_arguments',
    'cleanup_reinit', 'platform_resources',
}
ANSI = re.compile(r'\x1b\[[0-?]*[ -/]*[@-~]')


def fields(text):
    return dict(re.findall(r'(\w+)=([^\s]+)', text))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def analyze(texts, expected_elf, minimum=10):
    runs = []
    for text in texts:
        current = None
        for line in ANSI.sub('', text).splitlines():
            match = re.search(r'\bSEC003 (BEGIN|CASE|PROBE|RESOURCES|PUBLIC_BOOT_FINGERPRINT|RESULT)\b(.*)', line)
            if not match:
                continue
            kind, tail = match.groups()
            data = fields(tail)
            if kind == 'BEGIN':
                require(current is None, 'reset/new BEGIN before previous result')
                require(data.get('platform') == 'ESP32C6', 'not an ESP32-C6 log')
                require(data.get('idf') == 'v5.5.6' and data.get('mbed') == '3.6.7', 'wrong baseline')
                require(data.get('elf_sha256') == expected_elf, 'wrong/mixed ELF')
                current = {'cases': set(), 'reset_reason': data.get('reset_reason')}
                continue
            require(current is not None, 'missing BEGIN / partial capture')
            if kind == 'CASE':
                parts = tail.split()
                require(len(parts) == 2 and parts[0] in CASES and parts[1] == 'PASS', 'failed/unknown test case')
                require(parts[0] not in current['cases'], 'duplicate case')
                current['cases'].add(parts[0])
            elif kind == 'PUBLIC_BOOT_FINGERPRINT':
                require('fingerprint' not in current, 'duplicate fingerprint record')
                fingerprint = tail.strip()
                require(re.fullmatch('[0-9a-f]{64}', fingerprint) and int(fingerprint, 16), 'invalid public fingerprint')
                current['fingerprint'] = fingerprint
            elif kind in ('PROBE', 'RESOURCES'):
                require(kind not in current, 'duplicate diagnostic record')
                current[kind] = data
            elif kind == 'RESULT':
                parts = tail.split()
                require(parts and parts[0] == 'PASS', 'functional result FAIL/malformed')
                require(data == {'passed': '14', 'failed': '0', 'cleanup': '1'}, 'incomplete result')
                require(current['cases'] == CASES and 'fingerprint' in current, 'incomplete cases/fingerprint')
                probe = current.get('PROBE', {})
                require(int(probe.get('injected_failures', -1)) == 2 and
                        int(probe.get('entropy_calls', 0)) >= 5 and
                        int(probe.get('hardware_calls', 0)) >= 2 and
                        int(probe.get('hardware_bytes', 0)) > 0, 'missing entropy/reseed diagnostics')
                resources = current.get('RESOURCES', {})
                require(int(resources.get('stack_min_bytes', 0)) >= 512 and
                        int(resources.get('largest_block', 0)) > 0 and
                        0 < int(resources.get('elapsed_us', 0)) < 120_000_000, 'resource/time criterion failed')
                runs.append(current)
                current = None
        require(current is None, 'truncated run')
    require(len(runs) >= minimum, 'insufficient complete runs')
    require(len({r['fingerprint'] for r in runs}) == len(runs), 'repeated public-key fingerprint')
    return {
        'functional_log_check': 'PASS', 'runs': len(runs),
        'unique_public_fingerprints': len(runs), 'elf_sha256': expected_elf,
        'physical_execution_and_warm_cold_procedure': 'OPERATOR_REVIEW_REQUIRED',
        'entropy_quality': 'NOT_PROVEN', 'permanent_identities': 'NO_GO',
        'sec003_hardware_validation': 'PENDING_REVIEW',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', required=True, type=pathlib.Path)
    parser.add_argument('--min-runs', type=int, default=10)
    parser.add_argument('logs', nargs='+', type=pathlib.Path)
    args = parser.parse_args()
    if args.min_runs < 1:
        parser.error('--min-runs must be positive')
    try:
        digest = hashlib.sha256(args.elf.read_bytes()).hexdigest()
        result = analyze([p.read_text(errors='replace') for p in args.logs], digest, args.min_runs)
    except (OSError, ValueError) as error:
        print(json.dumps({'functional_log_check': 'FAIL', 'reason': str(error), 'permanent_identities': 'NO_GO'}))
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
