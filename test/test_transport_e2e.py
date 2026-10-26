#!/usr/bin/env python3
import os
import socket
import subprocess
import sys
import tempfile
import time
import unittest


class TransportIntegrationTest(unittest.TestCase):
    def test_c_application_transport_round_trip(self):
        if len(sys.argv) != 2:
            self.fail("endpoint executable argument missing")
        executable = sys.argv.pop()
        with tempfile.TemporaryDirectory() as directory:
            probe = socket.socket()
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
            probe.close()
            relay = subprocess.Popen(
                [sys.executable, "relay/falafel_relay.py", "--port", str(port),
                 "--data", os.path.join(directory, "relay.json")],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                deadline = time.monotonic() + 3
                while time.monotonic() < deadline:
                    try:
                        check = socket.create_connection(("127.0.0.1", port),
                                                         timeout=0.1)
                        check.close()
                        break
                    except OSError:
                        time.sleep(0.01)
                result = subprocess.run([executable, str(port)], timeout=12,
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0,
                                 result.stdout + result.stderr)
            finally:
                relay.terminate()
                relay.wait(timeout=3)


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
