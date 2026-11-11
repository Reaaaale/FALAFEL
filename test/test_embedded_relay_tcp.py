"""Host execution of the same registration-only TCP adapter compiled for C6."""
import pathlib
import select
import socket
import subprocess
import sys
import tempfile
import time
import unittest

PROBE = sys.argv.pop(1)
RELAY = pathlib.Path(__file__).resolve().parents[1] / "relay/falafel_relay.py"


class Transport(unittest.TestCase):
    def start(self, port, own=1, peer=2, host="127.0.0.1"):
        child = subprocess.Popen([PROBE, host, str(port), str(own), str(peer)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.stop, child)
        return child

    @staticmethod
    def stop(child):
        if child.poll() is None:
            child.terminate()
        child.communicate(timeout=5)

    def test_exact_registration_and_no_ack_for_incoming_bytes(self):
        with socket.socket() as server:
            server.bind(("127.0.0.1", 0)); server.listen(); server.settimeout(3)
            child = self.start(server.getsockname()[1])
            with server.accept()[0] as connection:
                connection.settimeout(3)
                hello = bytearray()
                while not hello.endswith(b"\n"):
                    hello.extend(connection.recv(1))
                self.assertEqual(hello, b"HELLO2 1 2\n")
                # Stand in for cached bytes: the preparation must never ACK.
                connection.sendall(b"\x00\x00")
                connection.sendall(b"\x00\x05opaque")
                self.assertEqual(connection.recv(1), b"")
            out, err = child.communicate(timeout=3)
            self.assertEqual(child.returncode, 0, err)
            self.assertIn("IDLE", out)

    def test_invalid_config_and_refused_connection(self):
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]  # bound but deliberately not listening
            for own, peer, host in ((0, 2, "127.0.0.1"), (1, 1, "127.0.0.1"),
                                    (1, 2, "invalid"), (1, 2, "127.0.0.1")):
                child = self.start(port, own, peer, host)
                out, _ = child.communicate(timeout=6)
                self.assertEqual(child.returncode, 1)
                self.assertEqual(out.strip(), "OPEN_FAILED")

    def test_existing_e2ee_relay_registration_pair_and_reconnect(self):
        with tempfile.TemporaryDirectory() as root:
            relay = subprocess.Popen([sys.executable, str(RELAY), "--e2ee-only", "--port", "0",
                                      "--data", str(pathlib.Path(root) / "relay.json")],
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.addCleanup(self.stop, relay)
            self.assertTrue(select.select([relay.stdout], [], [], 5)[0], "relay startup timeout")
            line = relay.stdout.readline()
            port = int(line.strip().rsplit(":", 1)[1])
            # Model reset without FIN: routing ID 1 remains occupied.
            # The C6 adapter must replace that stale connection.
            with socket.create_connection(("127.0.0.1", port), timeout=3) as owner:
                owner.sendall(b"HELLO2 1 2\n")
                time.sleep(.05)
                duplicate = self.start(port)
                out, err = duplicate.communicate(timeout=3)
                self.assertEqual(duplicate.returncode, 0, err)
                self.assertIn("IDLE", out)
                self.assertEqual(owner.recv(1), b"")
                peer = self.start(port, 2, 1)
                out, err = peer.communicate(timeout=3)
                self.assertEqual(peer.returncode, 0, err)
                self.assertIn("IDLE", out)
                invalid = self.start(port, 3, 1)
                out, _ = invalid.communicate(timeout=3)
                self.assertIn("CLOSED", out)
            relay.terminate(); relay.communicate(timeout=3)


if __name__ == "__main__":
    unittest.main()
