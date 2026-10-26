#!/usr/bin/env python3
"""Durable local Falafel relay. The protocol is cleartext and development-only."""

import argparse
import copy
import json
import os
import selectors
import socket
import struct
import tempfile

MAX_FRAME = 296
MAX_HANDSHAKE = 80
MAX_INPUT = 8192
MAX_OUTPUT = 65536
STATE_VERSION = 2
HEADER = struct.Struct(">2sBBIIQQQBBH")
RELAY_ID = 0xFFFFFFFF


class PersistenceError(Exception):
    """Raised when durable relay state cannot be committed."""


class Client:
    def __init__(self, sock):
        self.sock = sock
        self.input = bytearray()
        self.output = bytearray()
        self.device = None
        self.peer = None


class Relay:
    def __init__(self, host, port, data_file, allowed, max_pending,
                 max_completed=1024, persist_hook=None):
        self.selector = selectors.DefaultSelector()
        self.clients = {}
        self.by_device = {}
        self.data_file = data_file
        self.allowed = set(allowed)
        self.max_pending = max_pending
        self.max_completed = max_completed
        self.persist_hook = persist_hook
        self.state = {"version": STATE_VERSION, "pending": [], "completed": []}
        self._load()

        self.server = socket.socket()
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((host, port))
        self.server.listen()
        self.server.setblocking(False)
        self.selector.register(self.server, selectors.EVENT_READ, None)
        print(f"Falafel relay listening on {host}:{self.server.getsockname()[1]}",
              flush=True)

    @staticmethod
    def _record_from_frame(frame):
        fields = HEADER.unpack(frame[:HEADER.size])
        return {
            "key": f"{fields[3]}:{fields[5]}:{fields[2]}",
            "source": fields[3],
            "destination": fields[4],
            "event_id": fields[5],
            "kind": fields[2],
            "frame": frame.hex(),
        }

    def _validate_loaded_record(self, record, include_frame):
        required = {"key", "source", "destination", "event_id", "kind"}
        if not isinstance(record, dict) or not required.issubset(record):
            return False
        if (record["source"], record["destination"]) not in self.allowed:
            return False
        if include_frame:
            try:
                frame = bytes.fromhex(record["frame"])
                parsed = self._parse_frame(frame)
            except (KeyError, ValueError, struct.error):
                return False
            return (parsed[3] == record["source"] and
                    parsed[4] == record["destination"] and
                    parsed[5] == record["event_id"] and
                    parsed[2] == record["kind"])
        return True

    def _load(self):
        try:
            with open(self.data_file, encoding="utf-8") as stream:
                loaded = json.load(stream)
        except FileNotFoundError:
            return
        except (OSError, ValueError) as error:
            raise PersistenceError(f"cannot load relay state: {error}") from error

        if loaded.get("version") != STATE_VERSION:
            raise PersistenceError("unsupported relay state version")
        pending = loaded.get("pending")
        completed = loaded.get("completed")
        if not isinstance(pending, list) or not isinstance(completed, list):
            raise PersistenceError("invalid relay state structure")
        if len(pending) > self.max_pending or len(completed) > self.max_completed:
            raise PersistenceError("relay state exceeds configured limits")
        if not all(self._validate_loaded_record(item, True) for item in pending):
            raise PersistenceError("invalid pending relay record")
        if not all(self._validate_loaded_record(item, False) for item in completed):
            raise PersistenceError("invalid completed relay record")
        self.state = loaded

    def _save(self):
        if self.persist_hook is not None:
            self.persist_hook()
        directory = os.path.dirname(os.path.abspath(self.data_file))
        os.makedirs(directory, exist_ok=True)
        descriptor, temporary = tempfile.mkstemp(dir=directory, prefix=".relay-")
        try:
            with os.fdopen(descriptor, "w", encoding="utf-8") as stream:
                json.dump(self.state, stream, separators=(",", ":"), sort_keys=True)
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, self.data_file)
            try:
                directory_fd = os.open(directory, os.O_RDONLY)
                try:
                    os.fsync(directory_fd)
                finally:
                    os.close(directory_fd)
            except OSError:
                # Some filesystems do not support directory fsync. The file itself
                # has still been flushed and atomically replaced.
                pass
        except OSError as error:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass
            raise PersistenceError(f"cannot persist relay state: {error}") from error

    def _accept(self):
        sock, _ = self.server.accept()
        sock.setblocking(False)
        client = Client(sock)
        self.clients[sock] = client
        self.selector.register(sock, selectors.EVENT_READ, client)

    def _set_events(self, client):
        events = selectors.EVENT_READ
        if client.output:
            events |= selectors.EVENT_WRITE
        self.selector.modify(client.sock, events, client)

    def _drop(self, client):
        if client.device is not None and self.by_device.get(client.device) is client:
            del self.by_device[client.device]
        self.clients.pop(client.sock, None)
        try:
            self.selector.unregister(client.sock)
        except (KeyError, ValueError):
            pass
        client.sock.close()

    def _queue_frame(self, client, frame):
        encoded = struct.pack(">I", len(frame)) + frame
        if len(client.output) + len(encoded) > MAX_OUTPUT:
            self._drop(client)
            return False
        client.output.extend(encoded)
        self._set_events(client)
        return True

    def _write(self, client):
        if not client.output:
            self._set_events(client)
            return
        try:
            count = client.sock.send(client.output)
        except InterruptedError:
            return
        except BlockingIOError:
            return
        except OSError:
            self._drop(client)
            return
        if count == 0:
            self._drop(client)
            return
        del client.output[:count]
        self._set_events(client)

    def _read(self, client):
        try:
            data = client.sock.recv(4096)
        except InterruptedError:
            return
        except BlockingIOError:
            return
        except OSError:
            self._drop(client)
            return
        if not data:
            self._drop(client)
            return
        if len(client.input) + len(data) > MAX_INPUT:
            self._drop(client)
            return
        client.input.extend(data)
        if client.device is None and not self._consume_handshake(client):
            return
        self._consume_frames(client)

    def _consume_handshake(self, client):
        newline = client.input.find(b"\n")
        if newline < 0:
            if len(client.input) > MAX_HANDSHAKE:
                self._drop(client)
            return False
        try:
            word, own_text, peer_text = bytes(client.input[:newline]).decode(
                "ascii").split()
            own = int(own_text)
            peer = int(peer_text)
        except (ValueError, UnicodeError):
            self._drop(client)
            return False
        del client.input[:newline + 1]
        if (word != "HELLO" or (own, peer) not in self.allowed or
                own in self.by_device):
            self._drop(client)
            return False
        client.device = own
        client.peer = peer
        self.by_device[own] = client
        self._redeliver(own)
        return True

    def _consume_frames(self, client):
        while len(client.input) >= 4:
            size = struct.unpack(">I", client.input[:4])[0]
            if size < HEADER.size or size > MAX_FRAME:
                self._drop(client)
                return
            if len(client.input) < size + 4:
                return
            frame = bytes(client.input[4:size + 4])
            del client.input[:size + 4]
            try:
                self._process_frame(client, frame)
            except PersistenceError as error:
                print(f"relay persistence error: {error}", flush=True)
                # No acknowledgement is queued. The sender will retry.

    def _parse_frame(self, frame):
        if len(frame) < HEADER.size:
            raise ValueError("short frame")
        fields = HEADER.unpack(frame[:HEADER.size])
        magic, version, kind, source, destination = fields[:5]
        revision, personality, ack, length = fields[7:]
        if (magic != b"FL" or version != 1 or len(frame) != HEADER.size + length or
                (source, destination) not in self.allowed):
            raise ValueError("invalid frame header")
        if kind == 1 and (length == 0 or ack != 0):
            raise ValueError("invalid text event")
        if kind == 2 and (length != 0 or personality > 2 or revision == 0 or ack != 0):
            raise ValueError("invalid personality event")
        if kind == 3 and (length != 0 or ack != 2):
            raise ValueError("invalid device acknowledgement")
        if kind not in (1, 2, 3):
            raise ValueError("unknown event kind")
        return fields

    @staticmethod
    def _ack_frame(source, destination, event_id, timestamp, phase):
        return HEADER.pack(b"FL", 1, 3, source, destination, event_id,
                           timestamp, 0, 0, phase, 0)

    def _find_pending(self, key):
        return next((item for item in self.state["pending"]
                     if item["key"] == key), None)

    def _find_completed(self, key):
        return next((item for item in self.state["completed"]
                     if item["key"] == key), None)

    def _ack_sender_from_record(self, record, completed):
        sender = self.by_device.get(record["source"])
        if sender is None:
            return
        original = HEADER.unpack(bytes.fromhex(record.get("frame", ""))[:HEADER.size]) \
            if record.get("frame") else None
        timestamp = original[6] if original else 0
        self._queue_frame(sender, self._ack_frame(
            RELAY_ID, record["source"], record["event_id"], timestamp, 1))
        if completed:
            self._queue_frame(sender, self._ack_frame(
                RELAY_ID, record["source"], record["event_id"], timestamp, 2))

    def _process_original(self, client, frame, fields):
        record = self._record_from_frame(frame)
        completed = self._find_completed(record["key"])
        if completed is not None:
            self._ack_sender_from_record(completed, True)
            return
        pending = self._find_pending(record["key"])
        if pending is not None:
            self._ack_sender_from_record(pending, False)
            self._deliver(pending)
            return
        if len(self.state["pending"]) >= self.max_pending:
            # Explicit backpressure: retain the connection but send no acceptance.
            # The unchanged sender event remains retryable.
            return

        previous = copy.deepcopy(self.state)
        self.state["pending"].append(record)
        try:
            self._save()
        except PersistenceError:
            self.state = previous
            raise
        self._ack_sender_from_record(record, False)
        self._deliver(record)

    def _process_device_ack(self, client, fields, frame):
        source, destination, event_id = fields[3], fields[4], fields[5]
        match = next((item for item in self.state["pending"]
                      if item["source"] == destination and
                      item["destination"] == source and
                      item["event_id"] == event_id), None)
        if match is None:
            return

        previous = copy.deepcopy(self.state)
        self.state["pending"].remove(match)
        completed = {key: value for key, value in match.items() if key != "frame"}
        self.state["completed"].append(completed)
        if len(self.state["completed"]) > self.max_completed:
            self.state["completed"] = self.state["completed"][-self.max_completed:]
        try:
            self._save()
        except PersistenceError:
            self.state = previous
            raise

        sender = self.by_device.get(destination)
        if sender is not None:
            self._queue_frame(sender, frame)

    def _process_frame(self, client, frame):
        try:
            fields = self._parse_frame(frame)
        except (ValueError, struct.error):
            self._drop(client)
            return
        if client.device != fields[3]:
            self._drop(client)
            return
        if fields[2] == 3:
            self._process_device_ack(client, fields, frame)
        else:
            self._process_original(client, frame, fields)

    def _deliver(self, record):
        recipient = self.by_device.get(record["destination"])
        if recipient is not None:
            self._queue_frame(recipient, bytes.fromhex(record["frame"]))

    def _redeliver(self, device):
        for record in self.state["pending"]:
            if record["destination"] == device:
                self._deliver(record)

    def run(self):
        while True:
            for key, events in self.selector.select():
                if key.fileobj is self.server:
                    self._accept()
                    continue
                client = key.data
                if events & selectors.EVENT_READ and client.sock in self.clients:
                    self._read(client)
                if events & selectors.EVENT_WRITE and client.sock in self.clients:
                    self._write(client)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--data", default=".falafel-relay/state.json")
    parser.add_argument("--pair", default="1:2")
    parser.add_argument("--max-pending", type=int, default=512)
    parser.add_argument("--max-completed", type=int, default=1024)
    args = parser.parse_args()
    try:
        first, second = (int(value) for value in args.pair.split(":"))
    except ValueError:
        parser.error("pair must use the form positive_id:positive_id")
    if (first <= 0 or second <= 0 or first == second):
        parser.error("pair IDs must be positive and distinct")
    if args.max_pending <= 0 or args.max_completed <= 0:
        parser.error("queue limits must be positive")
    Relay(args.host, args.port, args.data,
          [(first, second), (second, first)],
          args.max_pending, args.max_completed).run()


if __name__ == "__main__":
    main()
