"""Synthetic parser fixtures only: these are NOT hardware results."""
import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sec003_analysis', ROOT / 'tools/analyze-sec003.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ELF = 'a' * 64


def synthetic_run(number=1):
    lines = [f'SEC003 BEGIN platform=ESP32C6 idf=v5.5.6 mbed=3.6.7 elf_sha256={ELF} reset_reason=1']
    lines += [f'SEC003 CASE {name} PASS' for name in sorted(module.CASES)]
    lines += ['SEC003 PROBE entropy_calls=8 injected_failures=2 hardware_calls=6 hardware_bytes=768',
              'SEC003 RESOURCES heap_before=400000 heap_after=400000 largest_block=350000 stack_min_bytes=1024 elapsed_us=6000000',
              f'SEC003 PUBLIC_BOOT_FINGERPRINT {number:064x}',
              'SEC003 RESULT PASS passed=14 failed=0 cleanup=1']
    return '\n'.join(lines)


class LogTests(unittest.TestCase):
    def test_ten_complete_synthetic_runs(self):
        result = module.analyze([synthetic_run(i) for i in range(1, 11)], ELF)
        self.assertEqual(result['runs'], 10)
        self.assertEqual(result['permanent_identities'], 'NO_GO')
        self.assertEqual(result['sec003_hardware_validation'], 'PENDING_REVIEW')

    def test_one_run_and_ansi(self):
        text = '\x1b[32mI (123) test: ' + synthetic_run() + '\x1b[0m'
        self.assertEqual(module.analyze([text], ELF, 1)['functional_log_check'], 'PASS')

    def test_host_output_rejected(self):
        with self.assertRaises(ValueError):
            module.analyze(['SEC003_HOST RESULT PASS hardware=NOT_RUN'], ELF, 1)

    def test_wrong_elf(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run()], 'b' * 64, 1)

    def test_repeated_fingerprint(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run(), synthetic_run()], ELF, 2)

    def test_missing_case(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().replace('SEC003 CASE init PASS', '')], ELF, 1)

    def test_duplicate_case(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().replace('SEC003 CASE init PASS', 'SEC003 CASE init PASS\nSEC003 CASE init PASS')], ELF, 1)

    def test_case_failure(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().replace('CASE init PASS', 'CASE init FAIL')], ELF, 1)

    def test_truncated(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().split('SEC003 RESULT')[0]], ELF, 1)

    def test_reset_before_completion(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().split('SEC003 RESULT')[0] + '\n' + synthetic_run(2)], ELF, 1)

    def test_no_hardware_poll(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().replace('hardware_calls=6', 'hardware_calls=0')], ELF, 1)

    def test_missing_injection(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().replace('injected_failures=2', 'injected_failures=0')], ELF, 1)

    def test_low_stack(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().replace('stack_min_bytes=1024', 'stack_min_bytes=128')], ELF, 1)

    def test_empty_result(self):
        with self.assertRaises(ValueError):
            module.analyze([synthetic_run().replace('RESULT PASS passed=14 failed=0 cleanup=1', 'RESULT')], ELF, 1)


if __name__ == '__main__':
    unittest.main()
