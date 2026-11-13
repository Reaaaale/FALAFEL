"""Same C6 lab/stream sources, NVS API model, real relay and real SDL simulator.
This does NOT execute ESP hardware, Wi-Fi or physical NVS."""
import os
from pathlib import Path
import select
import socket
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from generate_tls_credentials import generate

PROBE, SIM, FIXTURE = sys.argv[1:4]
del sys.argv[1:4]
REPO = Path(__file__).resolve().parents[1]


class C6Simulator(unittest.TestCase):
    def test_partial_tls_options_fail_closed(self):
        for options in (["--tls-ca"], ["--tls-key", "missing.pem"],
                        ["--tls-ca", "--tls-cert", "missing.pem"]):
            with self.subTest(options=options):
                result = subprocess.run([SIM, "--device", "2", "--peer", "1",
                    "--ui-smoke-test", *options], capture_output=True, timeout=5)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"Usage:", result.stderr)

    def test_bidirectional_partial_disconnect_and_reopen(self):
        self.exchange(False)

    def test_mtls_bidirectional_partial_disconnect_and_reopen(self):
        self.exchange(True)

    def exchange(self, mtls):
        with tempfile.TemporaryDirectory(prefix="falafel-c6-integration-") as directory:
            root = Path(directory)
            subprocess.run([FIXTURE, "provision", str(root)], check=True, capture_output=True)
            tls_relay, tls_sim, tls_c6 = [], [], []
            if mtls:
                credentials = generate(root / "tls", ["falafel-relay.lab"], ["127.0.0.1"])
                tls_relay = ["--tls-ca", str(credentials / "ca.pem"),
                    "--tls-cert", str(credentials / "server.pem"),
                    "--tls-key", str(credentials / "server.key.pem"),
                    "--tls-devices", str(credentials / "devices.json")]
                tls_sim = ["--tls-ca", str(credentials / "ca.pem"),
                    "--tls-cert", str(credentials / "simulator.pem"),
                    "--tls-key", str(credentials / "simulator.key.pem"),
                    "--tls-server-name", "falafel-relay.lab"]
                tls_c6 = [str(credentials / "ca.pem"), str(credentials / "c6.pem"),
                    str(credentials / "c6.key.pem"), "falafel-relay.lab"]
            processes = []
            try:
                relay = subprocess.Popen([sys.executable, str(REPO / "relay/falafel_relay.py"),
                    "--e2ee-only", "--port", "0", "--data", str(root / "relay.json")] + tls_relay,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                processes.append(relay)
                self.assertTrue(select.select([relay.stdout], [], [], 5)[0])
                port = int(relay.stdout.readline().strip().rsplit(":", 1)[1])
                env = dict(os.environ, SDL_VIDEODRIVER="dummy")
                simulator = subprocess.Popen([SIM, "--device", "2", "--peer", "1", "--host", "127.0.0.1",
                    "--port", str(port), "--identity", str(root / "2.identity"),
                    "--mailbox", str(root / "2.mail"), "--init-history", "--test-send-preset",
                    "--test-duration-ms", "8000"] + tls_sim, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
                processes.append(simulator)
                probe = subprocess.Popen([PROBE, str(root / "1.identity"), "127.0.0.1", str(port)] + tls_c6,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                processes.append(probe)
                out, err = probe.communicate(timeout=15)
                self.assertEqual(probe.returncode, 0, out + err)
                gui, guierr = simulator.communicate(timeout=15)
                self.assertEqual(simulator.returncode, 0, gui + guierr)
                self.assertEqual(gui.count("[E2EE] RECEIVED "), 1, gui)
                self.assertEqual(gui.count("[E2EE] DELIVERED "), 1, gui)
                self.assertRegex(gui, r"rows=2\b")
                self.assertIn("stored=1 delivered=1 received=1 restored=1", out)
                # No plaintext in relay or either persistent mailbox.
                for path in (root / "relay.json", root / "1.mail", root / "2.mail"):
                    if path.exists():
                        self.assertNotIn(b"Ciao dal Falafel ESP32-C6", path.read_bytes())
                        self.assertNotIn(b"Sto arrivando", path.read_bytes())
            finally:
                for child in processes:
                    if child.poll() is None: child.terminate()
                    child.communicate(timeout=5)


if __name__ == "__main__":
    unittest.main()
