#!/usr/bin/env python3
import os
from pathlib import Path
import json
import selectors
import select
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from relay.falafel_relay import (Client, HEADER, MAIL_HEADER, MAX_OUTPUT,
                                 PersistenceError, Relay)


def encrypted_data(event_id):
    """Maximum-size opaque DATA accepted by the relay; no identity provisioning."""
    mid = event_id.to_bytes(16, "big")
    envelope = b"FAE\x01\x01" + bytes([122]) + bytes(64) + mid + bytes(170)
    return MAIL_HEADER.pack(b"FL", 2, 4, 1, 2, mid, len(envelope)) + envelope


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

    def connect(self, own, peer, fragmented=False, version=1):
        sock = socket.create_connection(("127.0.0.1", self.port), timeout=1)
        sock.settimeout(1)
        hello = f"{'HELLO2' if version == 2 else 'HELLO'} {own} {peer}\n".encode()
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

    def test_large_replay_to_slow_tcp_client_after_restart(self):
        frames = [encrypted_data(i) for i in range(226)]
        self.assertGreater(sum(4 + len(f) for f in frames), MAX_OUTPUT)
        self.relay.stop()
        with open(self.state, "w", encoding="utf-8") as stream:
            json.dump({"version": 2, "pending": [Relay._record_from_frame(f)
                       for f in frames], "completed": []}, stream)
        self.restart()
        with self.connect(2, 1, version=2) as recipient:
            # Let registration run before this deliberately slow reader drains.
            time.sleep(0.05)
            expected = b"".join(struct.pack(">I", len(f)) + f for f in frames)
            received = bytearray()
            while len(received) < len(expected):
                part = recipient.recv(min(97, len(expected) - len(received)))
                self.assertTrue(part, "relay disconnected during large replay")
                received.extend(part)
                time.sleep(0.0001)
            self.assertEqual(received, expected)
            recipient.settimeout(0.05)
            with self.assertRaises(socket.timeout):
                recipient.recv(1)
        self.assertIsNone(self.relay.process.poll())
        with open(self.state, encoding="utf-8") as stream:
            self.assertEqual(len(json.load(stream)["pending"]), len(frames))


class RelayReconnectTest(unittest.TestCase):
    """Real TCP sockets, with explicit dispatch to reproduce a lost FIN/RST."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.relay = Relay("127.0.0.1", 0,
                           os.path.join(self.temporary.name, "state.json"),
                           [(1, 2), (2, 1)], 512, e2ee_only=True)
        self.addCleanup(self.relay.selector.close)
        self.addCleanup(self.relay.server.close)

    def connect(self, hello=b"HELLO2 1 2\n"):
        remote = socket.create_connection(self.relay.server.getsockname(), timeout=1)
        self.addCleanup(remote.close)
        self.assertTrue(select.select([self.relay.server], [], [], 1)[0])
        self.relay._accept()
        client = list(self.relay.clients.values())[-1]
        self.addCleanup(self.relay._drop, client)
        remote.sendall(hello)
        self.assertTrue(select.select([client.sock], [], [], 1)[0])
        self.relay._read(client)
        return client, remote

    def test_reset_without_fin_replaces_stale_session_and_replays(self):
        old, old_remote = self.connect()
        self.assertIs(self.relay.by_device[1], old)
        # A hardware reset can leave the server socket established indefinitely.
        # Do not send FIN/RST: the old registration must still be present.
        frame = encrypted_data(71)
        self.relay._process_frame(old, frame)
        saved = json.dumps(self.relay.state, sort_keys=True)
        disk = Path(self.relay.data_file).read_bytes()
        for _ in range(3):
            current, remote = self.connect()
            self.assertIs(self.relay.by_device[1], current)
            self.assertEqual(old.sock.fileno(), -1)
            self.assertNotIn(old.sock, self.relay.clients)
            self.assertEqual(old_remote.recv(1), b"")
            # A delayed cleanup/event for the old socket cannot evict the new ID.
            self.relay._drop(old)
            self.assertIs(self.relay.by_device[1], current)
            old, old_remote = current, remote
        peer, reader = self.connect(b"HELLO2 2 1\n")
        self.relay._write(peer)
        expected = struct.pack(">I", len(frame)) + frame
        received = bytearray()
        while len(received) < len(expected):
            part = reader.recv(len(expected) - len(received))
            self.assertTrue(part)
            received.extend(part)
        self.assertEqual(received, expected)
        self.assertEqual(json.dumps(self.relay.state, sort_keys=True), saved)
        self.assertEqual(Path(self.relay.data_file).read_bytes(), disk)

    def test_replacement_discards_partial_output_and_replays_complete_frame(self):
        old, _ = self.connect(b"HELLO2 2 1\n")
        frame = encrypted_data(72)
        self.relay.state["pending"] = [Relay._record_from_frame(frame)]
        self.relay._redeliver(2)
        # Old stream has already sent a prefix fragment when the device resets.
        old.sock.send(old.output[:3])
        del old.output[:3]
        current, reader = self.connect(b"HELLO2 2 1\n")
        self.assertIs(self.relay.by_device[2], current)
        self.assertEqual(old.sock.fileno(), -1)
        self.relay._write(current)
        expected = struct.pack(">I", len(frame)) + frame
        received = bytearray()
        while len(received) < len(expected):
            part = reader.recv(len(expected) - len(received))
            self.assertTrue(part)
            received.extend(part)
        self.assertEqual(received, expected)

    def test_mixed_mode_version_mismatch_cannot_evict_owner(self):
        self.relay.e2ee_only = False
        old, _ = self.connect()
        rejected, _ = self.connect(b"HELLO 1 2\n")
        self.assertEqual(rejected.sock.fileno(), -1)
        self.assertIs(self.relay.by_device[1], old)

    def test_fin_and_reset_release_registration(self):
        for reset in (False, True):
            with self.subTest(reset=reset):
                old, remote = self.connect()
                self.assertIs(self.relay.by_device[1], old)
                if reset:
                    remote.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER,
                                      struct.pack("ii", 1, 0))
                remote.close()
                self.assertTrue(self.relay.selector.select(timeout=1))
                self.relay._read(old)
                self.assertNotIn(1, self.relay.by_device)
                current, _ = self.connect()
                self.assertIs(self.relay.by_device[1], current)
                self.relay._drop(current)

    def test_invalid_registration_cannot_evict_owner(self):
        old, _ = self.connect()
        for hello in (b"HELLO2 1 3\n", b"HELLO 1 2\n", b"invalid\n"):
            with self.subTest(hello=hello):
                rejected, _ = self.connect(hello)
                self.assertEqual(rejected.sock.fileno(), -1)
                self.assertIs(self.relay.by_device[1], old)


class RelayReplayTest(unittest.TestCase):
    """Deterministic backpressure on real socket pairs, without timing assumptions."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.relay = Relay("127.0.0.1", 0,
                           os.path.join(self.temporary.name, "state.json"),
                           [(1, 2), (2, 1)], 512)
        self.addCleanup(self.relay.selector.close)
        self.addCleanup(self.relay.server.close)

    def register(self, version=2):
        class SlowSocket:
            def __init__(self, wrapped):
                self.wrapped = wrapped
                self.error = None

            def fileno(self):
                return self.wrapped.fileno()

            def send(self, data):
                if self.error:
                    error, self.error = self.error, None
                    raise error
                return self.wrapped.send(data[:37])

            def close(self):
                self.wrapped.close()

        left, right = socket.socketpair()
        left.setblocking(False)
        right.setblocking(False)
        wrapped = SlowSocket(left)
        self.addCleanup(wrapped.close)
        self.addCleanup(right.close)
        client = Client(wrapped)
        self.relay.clients[wrapped] = client
        self.relay.selector.register(wrapped, 1, client)
        client.input.extend(b"HELLO2 2 1\n" if version == 2 else b"HELLO 2 1\n")
        self.assertTrue(self.relay._consume_handshake(client))
        self.assertIs(self.relay.by_device.get(2), client,
                      "registration replay must not disconnect a slow client")
        return client, right

    def drain(self, client, reader, expected):
        received = bytearray()
        for _ in range(20000):
            self.assertLessEqual(len(client.output), MAX_OUTPUT)
            self.assertIs(self.relay.by_device.get(2), client)
            self.relay._write(client)
            try:
                part = reader.recv(97)
            except BlockingIOError:
                part = b""
            received.extend(part)
            if len(received) >= len(expected):
                break
        self.assertEqual(received, expected)
        self.assertEqual(client.output, b"")
        self.assertFalse(self.relay.selector.get_key(client.sock).events &
                         selectors.EVENT_WRITE)
        with self.assertRaises(BlockingIOError):
            reader.recv(1)

    def test_226_maximum_frames_partial_writes_and_reconnect(self):
        frames = [encrypted_data(i) for i in range(226)]
        expected = b"".join(struct.pack(">I", len(f)) + f for f in frames)
        self.assertGreater(len(expected), MAX_OUTPUT)
        self.relay.state["pending"] = [Relay._record_from_frame(f) for f in frames]
        saved = json.dumps(self.relay.state, sort_keys=True)
        client, reader = self.register()
        for error in (BlockingIOError(), InterruptedError()):
            before = bytes(client.output)
            client.sock.error = error
            self.relay._write(client)
            self.assertEqual(client.output, before)
        # Disconnect mid-frame; a fresh registration must replay complete frames.
        self.relay._write(client)
        client.sock.error = BrokenPipeError()
        self.relay._write(client)
        self.assertNotIn(2, self.relay.by_device)
        client, reader = self.register()
        self.drain(client, reader, expected)
        self.assertEqual(json.dumps(self.relay.state, sort_keys=True), saved)
        self.relay._drop(client)

    def test_live_delivery_and_retries_wait_behind_full_replay(self):
        frames = [encrypted_data(i) for i in range(512)]
        self.relay.state["pending"] = [Relay._record_from_frame(f) for f in frames[:226]]
        client, reader = self.register()
        for frame in frames[226:]:
            record = Relay._record_from_frame(frame)
            self.relay.state["pending"].append(record)
            self.relay._deliver(record)
            self.relay._deliver(record)  # repeated retry while still deferred
        expected = b"".join(struct.pack(">I", len(f)) + f for f in frames)
        self.drain(client, reader, expected)
        self.relay._drop(client)

    def test_legacy_ack_removes_deferred_record_without_skipping_others(self):
        frames = [event(1, 1, 2, i, bytes(256)) for i in range(226)]
        self.relay.state["pending"] = [Relay._record_from_frame(f) for f in frames]
        client, reader = self.register(version=1)
        ack = event(3, 2, 1, 224, ack=2)
        self.relay._process_frame(client, ack)
        self.assertIsNone(self.relay._find_pending("1:224:1"))
        self.assertNotIn("1:224:1", client.deliveries)
        expected = b"".join(struct.pack(">I", len(f)) + f
                            for i, f in enumerate(frames) if i != 224)
        self.drain(client, reader, expected)
        self.assertEqual(len(self.relay.state["completed"]), 1)
        self.relay._drop(client)


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
