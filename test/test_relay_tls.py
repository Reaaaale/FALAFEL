#!/usr/bin/env python3
"""Temporary laboratory PKI, real SSL sockets and actual C client verifiers."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import select
import selectors
import socket
import ssl
import struct
import subprocess
import sys
import tempfile
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(REPO / "tools"))
from generate_tls_credentials import generate
from relay.falafel_relay import Relay, Client
from relay.tls_auth import load_devices

PROBE = None


def resolve_probe(argv):
    """Keep unittest arguments intact; never silently omit the real C checks."""
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--probe", help="path to the CMake tls_client_probe executable")
    options, remaining = parser.parse_known_args(argv)
    # Preserve the original CTest/standalone positional-path interface.
    legacy = None
    if remaining and not remaining[0].startswith("-") and (
            "/" in remaining[0] or Path(remaining[0]).name == "tls_client_probe"):
        legacy = remaining.pop(0)
    if options.probe and legacy:
        parser.error("specify the probe only once")
    selected = options.probe or legacy or os.environ.get("FALAFEL_TLS_PROBE")
    probe = Path(selected).expanduser() if selected else REPO / "build/tests/tls_client_probe"
    if not probe.is_file() or not os.access(probe, os.X_OK):
        parser.error(f"C client probe is missing or not executable: {probe}. "
                     "Build it with 'cmake -S . -B build/tests' and "
                     "'cmake --build build/tests --target tls_client_probe', "
                     "or use --probe PATH / FALAFEL_TLS_PROBE for another build tree.")
    return str(probe.resolve()), remaining


HEADER = struct.Struct(">2sBBII16sH")


def frame(source=1, destination=2, number=1):
    mid = number.to_bytes(16, "big")
    envelope = b"FAE\x01\x01" + bytes([122]) + bytes(64) + mid + bytes(170)
    wire = HEADER.pack(b"FL", 2, 4, source, destination, mid, len(envelope)) + envelope
    return struct.pack(">I", len(wire)) + wire


def exact(sock, n):
    data = bytearray()
    while len(data) < n:
        part = sock.recv(n - len(data))
        if not part:
            raise EOFError("closed before complete frame")
        data.extend(part)
    return bytes(data)


class MTLS(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="falafel-mtls-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        cls.pki = generate(cls.root / "pki", ["falafel-relay.lab"], ["127.0.0.1"])
        cls.foreign = generate(cls.root / "foreign", ["foreign.lab"], [])

    def setUp(self):
        self.state = self.root / f"state-{self._testMethodName}.json"
        self.children = []
        self.addCleanup(self.stop_all)
        self.start_relay()

    def stop_all(self):
        for process in self.children:
            if process.poll() is None:
                process.terminate()
            process.communicate(timeout=5)

    def start_relay(self, *extra):
        relay = subprocess.Popen([sys.executable, str(REPO / "relay/falafel_relay.py"),
            "--e2ee-only", "--port", "0", "--data", str(self.state),
            "--tls-ca", str(self.pki / "ca.pem"), "--tls-cert", str(self.pki / "server.pem"),
            "--tls-key", str(self.pki / "server.key.pem"),
            "--tls-devices", str(self.pki / "devices.json"), *extra],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.children.append(relay)
        self.assertTrue(select.select([relay.stdout], [], [], 5)[0], "relay startup timeout")
        line = relay.stdout.readline().strip()
        self.assertIn("listening", line, relay.stderr.read() if relay.poll() is not None else line)
        self.port = int(line.rsplit(":", 1)[1])
        self.relay = relay

    def context(self, client="c6", pki=None):
        context = ssl.create_default_context(cafile=str(self.pki / "ca.pem"))
        context.minimum_version = ssl.TLSVersion.TLSv1_2
        if client:
            pki = pki or self.pki
            context.load_cert_chain(str(pki / f"{client}.pem"), str(pki / f"{client}.key.pem"))
        return context

    def connect(self, client="c6", own=1, peer=2, name="falafel-relay.lab", context=None):
        raw = socket.create_connection(("127.0.0.1", self.port), timeout=3)
        self.addCleanup(raw.close)
        secure = (context or self.context(client)).wrap_socket(raw, server_hostname=name)
        self.addCleanup(secure.close)
        secure.sendall(f"HELLO2 {own} {peer}\n".encode())
        return secure

    def assert_rejected(self, client="c6", own=1, peer=2, context=None):
        try:
            sock = self.connect(client, own, peer, context=context)
            # TLS 1.3 can report certificate rejection only on application I/O.
            self.assertEqual(sock.recv(1), b"")
        except (ssl.SSLError, ConnectionError, EOFError):
            pass
        self.assertIsNone(self.relay.poll())

    def test_both_clients_bidirectional_tls12_and_tls13(self):
        versions = [ssl.TLSVersion.TLSv1_2]
        if ssl.HAS_TLSv1_3:
            versions.append(ssl.TLSVersion.TLSv1_3)
        for version in versions:
            with self.subTest(version=version):
                # Independent durable cache for each negotiated TLS version.
                self.state = self.root / f"version-{int(version)}.json"
                self.start_relay()
                first_context = self.context("c6")
                second_context = self.context("simulator")
                first_context.minimum_version = first_context.maximum_version = version
                second_context.minimum_version = second_context.maximum_version = version
                first = self.connect(context=first_context)
                second = self.connect("simulator", 2, 1, context=second_context)
                forward, reverse = frame(number=int(version)), frame(2, 1, int(version))
                first.sendall(forward)
                self.assertEqual(exact(second, len(forward)), forward)
                second.sendall(reverse)
                self.assertEqual(exact(first, len(reverse)), reverse)
                first.close(); second.close()

    def test_client_without_certificate_rejected(self):
        self.assert_rejected(context=self.context(None))

    def test_foreign_ca_client_rejected(self):
        self.assert_rejected(context=self.context("c6", self.foreign))

    def test_certificate_cannot_claim_other_device_or_evict_it(self):
        owner = self.connect("simulator", 2, 1)
        self.assert_rejected(own=2, peer=1)
        first = self.connect()
        data = frame(number=22)
        first.sendall(data)
        self.assertEqual(exact(owner, len(data)), data)

    def test_server_endpoint_mismatch_rejected(self):
        with self.assertRaises(ssl.SSLCertVerificationError):
            self.connect(name="wrong.lab")

    def test_unknown_fingerprint_rejected_even_with_trusted_ca(self):
        mapping = json.loads((self.pki / "devices.json").read_text())
        mapping = {key: value for key, value in mapping.items() if value == 2}
        path = self.root / "simulator-only.json"
        path.write_text(json.dumps(mapping))
        self.start_relay("--tls-devices", str(path))
        self.assert_rejected()

    def test_replacement_after_lost_fin_and_rst_preserves_disk(self):
        old = self.connect()
        second = self.connect("simulator", 2, 1)
        data = frame(number=40)
        old.sendall(data)
        self.assertEqual(exact(second, len(data)), data)
        saved = self.state.read_bytes()
        replacement = self.connect()  # old socket deliberately still open
        replacement.sendall(data)
        self.assertEqual(exact(second, len(data)), data)
        self.assertEqual(self.state.read_bytes(), saved)
        try:
            self.assertEqual(old.recv(1), b"")
        except (ssl.SSLError, ConnectionError):
            pass
        replacement.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        replacement.close()
        fresh = self.connect()
        fresh.sendall(data)
        self.assertEqual(exact(second, len(data)), data)
        self.assertEqual(self.state.read_bytes(), saved)
        self.assertEqual(len(json.loads(saved)["pending"]), 1)

    def test_slow_fragmented_large_replay(self):
        first = self.connect()
        for number in range(226):
            first.sendall(frame(number=number))
        second = self.connect("simulator", 2, 1)
        received = [exact(second, 290) for _ in range(226)]
        self.assertEqual(set(received), {frame(number=n) for n in range(226)})

    def test_handshake_timeout_and_pending_limit(self):
        self.start_relay("--handshake-timeout", "0.4", "--max-pending-connections", "1")
        stalled = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        self.addCleanup(stalled.close)
        # The listener accepts these sequential TCP connects in backlog order.
        extra = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        self.addCleanup(extra.close)
        self.assertEqual(extra.recv(1), b"")
        self.assertEqual(stalled.recv(1), b"")
        first = self.connect()
        wire = frame(number=55)
        first.sendall(wire)
        # TLS completion/sendall is not a server-side HELLO2 acknowledgement.
        # A persisted frame proves registration released the sole pending slot.
        deadline = time.monotonic() + first.gettimeout()
        while not self.state.exists():
            self.assertLess(time.monotonic(), deadline, "first client was not registered")
            time.sleep(0.005)
        self.assertEqual(len(json.loads(self.state.read_text())["pending"]), 1)
        second = self.connect("simulator", 2, 1)
        self.assertEqual(exact(second, len(wire)), wire)
        reverse = frame(2, 1, 55)
        second.sendall(reverse)
        self.assertEqual(exact(first, len(reverse)), reverse)

    def test_authenticated_connection_without_hello_times_out(self):
        self.start_relay("--handshake-timeout", "0.4")
        raw = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        self.addCleanup(raw.close)
        secure = self.context().wrap_socket(raw, server_hostname="falafel-relay.lab")
        self.addCleanup(secure.close)
        self.assertEqual(secure.recv(1), b"")
        self.assertIsNone(self.relay.poll())

    def test_total_connection_limit_includes_registered_devices(self):
        self.start_relay("--max-clients", "2")
        first = self.connect()
        second = self.connect("simulator", 2, 1)
        wire = frame(number=56)
        first.sendall(wire)
        self.assertEqual(exact(second, len(wire)), wire)
        extra = socket.create_connection(("127.0.0.1", self.port), timeout=2)
        self.addCleanup(extra.close)
        self.assertEqual(extra.recv(1), b"")
        reverse = frame(2, 1, 57)
        second.sendall(reverse)
        self.assertEqual(exact(first, len(reverse)), reverse)

    def test_plaintext_not_accepted_and_incomplete_config_fails(self):
        # Exercise both plaintext rejection and the fatal-alert path even on
        # OpenSSL builds that close HELLO2 without sending any TLS record.
        for payload in (b"HELLO2 1 2\n", b"\x16\x03\x03\x00\x04\xff\x00\x00\x00"):
            with self.subTest(payload=payload):
                raw = socket.create_connection(("127.0.0.1", self.port), timeout=2)
                self.addCleanup(raw.close)
                raw.sendall(payload)
                response = bytearray()
                try:
                    while True:
                        chunk = raw.recv(4096)
                        if not chunk:
                            break
                        response.extend(chunk)
                        self.assertLessEqual(len(response), 7, "unexpected plaintext/TLS response")
                except ConnectionResetError:
                    pass
                if response:
                    # Some OpenSSL versions send a fatal TLS alert before closing.
                    # Require a complete alert record, never arbitrary response bytes.
                    self.assertEqual(len(response), 7)
                    self.assertEqual(response[:2], b"\x15\x03")
                    self.assertIn(response[2], (0, 1, 2, 3))
                    self.assertEqual(response[3:6], b"\x00\x02\x02")
        self.assertIsNone(self.relay.poll())
        self.assertFalse(self.state.exists(), "plaintext client created relay state")
        # A subsequent authenticated pair must still work normally.
        first = self.connect()
        second = self.connect("simulator", 2, 1)
        wire = frame(number=58)
        first.sendall(wire)
        self.assertEqual(exact(second, len(wire)), wire)
        for options in (["--tls-ca", str(self.pki / "ca.pem")], ["--tls-ca", ""],
                ["--tls-ca", "", "--tls-cert", "", "--tls-key", "", "--tls-devices", ""]):
            process = subprocess.run([sys.executable, str(REPO / "relay/falafel_relay.py"),
                *options], capture_output=True, timeout=5)
            self.assertNotEqual(process.returncode, 0)
            self.assertIn(b"no TCP fallback", process.stderr)

    def test_actual_c_client_verification_and_no_fallback(self):
        for mode in ("c6", "desktop"):
            for name, ca, success in (("falafel-relay.lab", self.pki / "ca.pem", True),
                    ("127.0.0.1", self.pki / "ca.pem", True),
                    ("wrong.lab", self.pki / "ca.pem", False),
                    ("falafel-relay.lab", self.foreign / "ca.pem", False)):
                with self.subTest(mode=mode, name=name, ca=ca):
                    result = subprocess.run([PROBE, mode, "127.0.0.1", str(self.port), name,
                        str(ca), str(self.pki / "c6.pem"), str(self.pki / "c6.key.pem")],
                        capture_output=True, text=True, timeout=6)
                    self.assertEqual(result.returncode, 0 if success else 1, result.stderr)

    def test_c_clients_reject_wrong_key_and_insecure_key_permissions(self):
        insecure = self.root / "insecure.key.pem"
        insecure.write_bytes((self.pki / "c6.key.pem").read_bytes())
        insecure.chmod(0o644)
        try:
            for mode in ("c6", "desktop"):
                for key in (self.pki / "simulator.key.pem", insecure):
                    with self.subTest(mode=mode, key=key):
                        result = subprocess.run([PROBE, mode, "127.0.0.1", str(self.port),
                            "falafel-relay.lab", str(self.pki / "ca.pem"), str(self.pki / "c6.pem"),
                            str(key)], capture_output=True, timeout=6)
                        self.assertEqual(result.returncode, 1, result.stderr)
        finally:
            insecure.unlink()

    def test_generated_certificates_have_distinct_x509_purposes(self):
        for leaf, purpose, success in (("server", "sslserver", True), ("c6", "sslclient", True),
                ("simulator", "sslclient", True), ("c6", "sslserver", False),
                ("server", "sslclient", False)):
            with self.subTest(leaf=leaf, purpose=purpose):
                result = subprocess.run(["openssl", "verify", "-CAfile", str(self.pki / "ca.pem"),
                    "-purpose", purpose, str(self.pki / f"{leaf}.pem")], capture_output=True, timeout=5)
                self.assertEqual(result.returncode == 0, success)

    def test_provisioning_refuses_overwrite_and_separates_keys(self):
        with self.assertRaises(FileExistsError):
            generate(self.pki, ["falafel-relay.lab"], [])
        with self.assertRaises(ValueError):
            generate(REPO / "tls-credentials", ["falafel-relay.lab"], [])
        self.assertNotEqual((self.pki / "c6.key.pem").read_bytes(),
                            (self.pki / "simulator.key.pem").read_bytes())
        self.assertEqual(set(load_devices(self.pki / "devices.json").values()), {1, 2})
        for path in self.pki.iterdir():
            self.assertEqual(path.stat().st_mode & 0o077, 0)


class TLSWantIO(unittest.TestCase):
    def test_cross_direction_wants_and_stable_write_retry(self):
        with tempfile.TemporaryDirectory() as root:
            relay = Relay("127.0.0.1", 0, str(Path(root) / "state"), [(1, 2)], 2,
                          tls_context=object(), tls_devices={"fixture": 1})
            self.addCleanup(relay.selector.close)
            self.addCleanup(relay.server.close)
            left, right = socket.socketpair()
            self.addCleanup(right.close)
            class Socket:
                def __init__(self):
                    self.writes = []
                    self.send_error = ssl.SSLWantReadError()
                    self.read_error = ssl.SSLWantWriteError()
                def fileno(self): return left.fileno()
                def close(self): left.close()
                def send(self, data):
                    self.writes.append(bytes(data))
                    if self.send_error:
                        error, self.send_error = self.send_error, None
                        raise error
                    return len(data)
                def recv(self, count):
                    error, self.read_error = self.read_error, ssl.SSLWantReadError()
                    raise error
            sock = Socket()
            client = Client(sock)
            self.addCleanup(relay._drop, client)
            relay.clients[sock] = client
            relay.selector.register(sock, selectors.EVENT_READ, client)
            client.output.extend(b"initial")
            relay._write(client)
            self.assertTrue(client.write_wants_read)
            self.assertFalse(relay.selector.get_key(sock).events & selectors.EVENT_WRITE)
            client.output.extend(b"later")
            relay._write(client)
            self.assertEqual(sock.writes, [b"initial", b"initial"])
            self.assertEqual(client.output, b"later")
            relay._read(client)
            self.assertTrue(relay.selector.get_key(sock).events & selectors.EVENT_WRITE)
            relay._read(client)
            self.assertFalse(client.read_wants_write)
            sock.send_error = ssl.SSLWantWriteError()
            relay._write(client)
            self.assertTrue(relay.selector.get_key(sock).events & selectors.EVENT_WRITE)
            relay._write(client)
            self.assertEqual(client.output, b"")


if __name__ == "__main__":
    PROBE, arguments = resolve_probe(sys.argv[1:])
    unittest.main(argv=[sys.argv[0], *arguments])
