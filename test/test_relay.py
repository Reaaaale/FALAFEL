#!/usr/bin/env python3
import os
import json
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from relay.falafel_relay import Client, HEADER, PersistenceError, Relay


def event(kind, source, destination, event_id, payload=b"", revision=0,
          personality=0, ack=0):
    return HEADER.pack(b"FL", 1, kind, source, destination, event_id, 1,
                       revision, personality, ack, len(payload)) + payload


def send_frame(sock, frame, fragment=False):
    encoded = struct.pack(">I", len(frame)) + frame
    if fragment:
        for byte in encoded:
            sock.sendall(bytes([byte]))
    else:
        sock.sendall(encoded)


def receive(sock):
    header = b""
    while len(header) < 4:
        data = sock.recv(4 - len(header))
        if not data:
            raise EOFError
        header += data
    size = struct.unpack(">I", header)[0]
    data = b""
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise EOFError
        data += part
    return HEADER.unpack(data[:HEADER.size]), data[HEADER.size:]


class RelayProcess:
    def __init__(self, port, state, max_pending=512):
        self.port = port
        self.process = subprocess.Popen(
            [sys.executable, "relay/falafel_relay.py", "--port", str(port),
             "--data", state, "--max-pending", str(max_pending)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            try:
                probe = socket.create_connection(("127.0.0.1", port), timeout=0.1)
                probe.close()
                return
            except OSError:
                time.sleep(0.01)
        raise RuntimeError("relay did not start")

    def stop(self):
        if self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=3)
        if self.process.stdout is not None:
            self.process.stdout.close()
        if self.process.stderr is not None:
            self.process.stderr.close()


class RelayTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        probe = socket.socket()
        probe.bind(("127.0.0.1", 0))
        self.port = probe.getsockname()[1]
        probe.close()
        self.state = os.path.join(self.temporary.name, "state.json")
        self.relay = RelayProcess(self.port, self.state)

    def tearDown(self):
        self.relay.stop()
        self.temporary.cleanup()

    def connect(self, own, peer, fragmented=False):
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=1)
        sock.settimeout(1)
        hello = f"HELLO {own} {peer}\n".encode()
        if fragmented:
            for byte in hello:
                sock.sendall(bytes([byte]))
        else:
            sock.sendall(hello)
        return sock

    def restart(self, max_pending=512):
        self.relay.stop()
        self.relay = RelayProcess(self.port, self.state, max_pending)

    def test_fragmented_online_delivery_and_duplicate(self):
        first = self.connect(1, 2, fragmented=True)
        second = self.connect(2, 1)
        message = event(1, 1, 2, 10, b"ciao")
        send_frame(first, message, fragment=True)
        self.assertEqual(receive(first)[0][9], 1)
        self.assertEqual(receive(second)[1], b"ciao")
        send_frame(first, message)
        self.assertEqual(receive(first)[0][9], 1)
        self.assertEqual(receive(second)[1], b"ciao")
        send_frame(second, event(3, 2, 1, 10, ack=2), fragment=True)
        self.assertEqual(receive(first)[0][9], 2)
        first.close()
        second.close()

    def test_restart_pending_and_final_status_recovery(self):
        first = self.connect(1, 2)
        message = event(1, 1, 2, 11, b"offline")
        send_frame(first, message)
        self.assertEqual(receive(first)[0][9], 1)
        first.close()
        self.restart()

        second = self.connect(2, 1)
        delivered, payload = receive(second)
        self.assertEqual(payload, b"offline")
        send_frame(second, event(3, 2, 1, delivered[5], ack=2))
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            try:
                with open(self.state, encoding="utf-8") as stream:
                    if json.load(stream)["completed"]:
                        break
            except (OSError, ValueError):
                pass
            time.sleep(0.01)
        else:
            self.fail("recipient acknowledgement was not persisted")
        second.close()
        self.restart()

        first = self.connect(1, 2)
        send_frame(first, message)
        phases = {receive(first)[0][9], receive(first)[0][9]}
        self.assertEqual(phases, {1, 2})
        first.close()

    def test_full_queue_applies_backpressure(self):
        self.restart(max_pending=1)
        first = self.connect(1, 2)
        send_frame(first, event(1, 1, 2, 20, b"one"))
        self.assertEqual(receive(first)[0][9], 1)
        send_frame(first, event(1, 1, 2, 21, b"two"))
        with self.assertRaises(socket.timeout):
            receive(first)
        first.close()

    def test_corrupt_and_unsupported_state_are_rejected(self):
        self.relay.stop()
        with open(self.state, "w", encoding="utf-8") as stream:
            stream.write("not-json")
        process = subprocess.run(
            [sys.executable, "relay/falafel_relay.py", "--port", str(self.port),
             "--data", self.state], capture_output=True, text=True)
        self.assertNotEqual(process.returncode, 0)
        with open(self.state, "w", encoding="utf-8") as stream:
            stream.write('{"version":99,"pending":[],"completed":[]}')
        process = subprocess.run(
            [sys.executable, "relay/falafel_relay.py", "--port", str(self.port),
             "--data", self.state], capture_output=True, text=True)
        self.assertNotEqual(process.returncode, 0)


class RelayFaultInjectionTest(unittest.TestCase):
    def test_no_ack_or_pending_record_when_persistence_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            def fail_save():
                raise PersistenceError("injected")

            relay = Relay("127.0.0.1", 0, os.path.join(directory, "state.json"),
                          [(1, 2), (2, 1)], 4, persist_hook=fail_save)
            left, right = socket.socketpair()
            left.setblocking(False)
            client = Client(left)
            client.device = 1
            client.peer = 2
            relay.clients[left] = client
            relay.by_device[1] = client
            relay.selector.register(left, 1, client)
            frame = event(1, 1, 2, 30, b"not-durable")
            with self.assertRaises(PersistenceError):
                relay._process_original(client, frame, HEADER.unpack(frame[:40]))
            self.assertEqual(relay.state["pending"], [])
            self.assertEqual(client.output, b"")
            relay._drop(client)
            right.close()
            relay.selector.unregister(relay.server)
            relay.server.close()

    def test_buffered_output_handles_partial_writes(self):
        class PartialSocket:
            def __init__(self, wrapped):
                self.wrapped = wrapped

            def fileno(self):
                return self.wrapped.fileno()

            def send(self, data):
                return self.wrapped.send(data[:3])

            def close(self):
                self.wrapped.close()

        with tempfile.TemporaryDirectory() as directory:
            relay = Relay("127.0.0.1", 0, os.path.join(directory, "state.json"),
                          [(1, 2), (2, 1)], 4)
            left, right = socket.socketpair()
            left.setblocking(False)
            wrapped = PartialSocket(left)
            client = Client(wrapped)
            relay.clients[wrapped] = client
            relay.selector.register(wrapped, 1, client)
            frame = event(1, 1, 2, 40, b"fragmented-output")
            self.assertTrue(relay._queue_frame(client, frame))
            while client.output:
                relay._write(client)
            expected = struct.pack(">I", len(frame)) + frame
            received = b""
            while len(received) < len(expected):
                received += right.recv(len(expected) - len(received))
            self.assertEqual(received, expected)
            relay._drop(client)
            right.close()
            relay.selector.unregister(relay.server)
            relay.server.close()


if __name__ == "__main__":
    unittest.main()
