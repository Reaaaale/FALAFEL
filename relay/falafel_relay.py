#!/usr/bin/env python3
"""Durable development relay: legacy v1 or opaque encrypted v2 envelopes."""

import argparse
import hashlib
import ssl
import time
import copy
import json
import os
import selectors
import socket
import struct
import tempfile

MAIL_HEADER = struct.Struct(">2sBBII16sH")
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
        self.version = 1
        # Ordered, coalesced keys into durable pending state, not another copy
        # of the backlog. At most one waiting delivery per cached record.
        self.deliveries = {}
        self.tls_handshake = False
        self.auth_device = None
        self.deadline = 0
        self.read_wants_write = False
        self.write_wants_read = False
        self.tls_write = None


class Relay:
    def __init__(self, host, port, data_file, allowed, max_pending,
                 max_completed=1024, persist_hook=None, e2ee_only=False,
                 tls_context=None, tls_devices=None, handshake_timeout=10.0,
                 max_clients=64, max_pending_connections=16):
        if handshake_timeout <= 0 or max_clients <= 0 or max_pending_connections <= 0:
            raise ValueError("connection limits must be positive")
        if tls_context is not None and not tls_devices:
            raise ValueError("mTLS requires certificate-to-device authorization")
        self.tls_context = tls_context
        self.tls_devices = dict(tls_devices or {})
        self.handshake_timeout = handshake_timeout
        self.max_clients = max_clients
        self.max_pending_connections = max_pending_connections
        self.e2ee_only = e2ee_only
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
        if frame[2] == 2:
            fields = MAIL_HEADER.unpack(frame[:MAIL_HEADER.size])
            return {"key": f"e:{fields[3]}:{fields[5].hex()}:{fields[2]}",
                    "source": fields[3], "destination": fields[4],
                    "event_id": fields[5].hex(), "kind": fields[2],
                    "frame": frame.hex(), "version": 2}
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
            expected = self._record_from_frame(frame)
            return (expected["key"] == record["key"] and
                    expected["event_id"] == record["event_id"] and
                    expected.get("version", 1) == record.get("version", 1) and
                    parsed[3] == record["source"] and
                    parsed[4] == record["destination"] and
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
        if self.e2ee_only and any(r.get("version", 1) != 2
                                  for r in pending + completed):
            raise PersistenceError("plaintext state rejected in E2EE-only mode")
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
                if any(r.get("version") == 2 for r in self.state["pending"]):
                    raise  # encrypted offline delivery requires durable directory replacement
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
        try:
            sock, _ = self.server.accept()
        except (BlockingIOError, InterruptedError):
            return
        pending = sum(c.device is None for c in self.clients.values())
        if len(self.clients) >= self.max_clients or pending >= self.max_pending_connections:
            sock.close()
            return
        sock.setblocking(False)
        try:
            if self.tls_context is not None:
                sock = self.tls_context.wrap_socket(sock, server_side=True,
                    do_handshake_on_connect=False, suppress_ragged_eofs=False)
        except (OSError, ssl.SSLError):
            sock.close()
            return
        client = Client(sock)
        client.tls_handshake = self.tls_context is not None
        client.deadline = time.monotonic() + self.handshake_timeout
        self.clients[sock] = client
        self.selector.register(sock, selectors.EVENT_READ, client)

    def _tls_handshake(self, client):
        try:
            client.sock.do_handshake()
            certificate = client.sock.getpeercert(binary_form=True)
            client.auth_device = self.tls_devices.get(
                hashlib.sha256(certificate).hexdigest()) if certificate else None
            if client.auth_device is None:
                self._drop(client)
                return
        except ssl.SSLWantReadError:
            self.selector.modify(client.sock, selectors.EVENT_READ, client)
            return
        except ssl.SSLWantWriteError:
            self.selector.modify(client.sock, selectors.EVENT_WRITE, client)
            return
        except (ssl.SSLError, OSError):
            self._drop(client)
            return
        client.tls_handshake = False
        self._set_events(client)

    def _set_events(self, client):
        events = selectors.EVENT_READ
        if client.read_wants_write or (client.output and not client.write_wants_read):
            events |= selectors.EVENT_WRITE
        self.selector.modify(client.sock, events, client)

    def _expire_clients(self):
        now = time.monotonic()
        for client in list(self.clients.values()):
            if client.device is None and now >= client.deadline:
                self._drop(client)

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
            # OpenSSL requires identical bytes across WANT retries, even if
            # additional cached frames have since been appended to output.
            if self.tls_context is not None and client.tls_write is None:
                client.tls_write = bytes(client.output)
            count = client.sock.send(client.tls_write if client.tls_write is not None else client.output)
        except ssl.SSLWantReadError:
            client.write_wants_read = True
            self._set_events(client)
            return
        except ssl.SSLWantWriteError:
            client.write_wants_read = False
            self._set_events(client)
            return
        except (InterruptedError, BlockingIOError):
            return
        except OSError:
            self._drop(client)
            return
        client.tls_write = None
        client.write_wants_read = False
        if count == 0:
            self._drop(client)
            return
        del client.output[:count]
        self._fill_deliveries(client)
        self._set_events(client)

    def _fill_deliveries(self, client):
        """Queue complete frames only when they fit; resume after socket writes."""
        while client.deliveries:
            key = next(iter(client.deliveries))
            record = self._find_pending(key)
            # Legacy ACKs may retire a record while this client is backpressured.
            if record is None or record.get("version", 1) != client.version:
                del client.deliveries[key]
                continue
            frame = bytes.fromhex(record["frame"])
            if len(client.output) + 4 + len(frame) > MAX_OUTPUT:
                return
            del client.deliveries[key]
            self._queue_frame(client, frame)

    def _read(self, client):
        try:
            data = client.sock.recv(4096)
        except ssl.SSLWantReadError:
            client.read_wants_write = False
            self._set_events(client)
            return
        except ssl.SSLWantWriteError:
            client.read_wants_write = True
            self._set_events(client)
            return
        except InterruptedError:
            return
        except BlockingIOError:
            return
        except OSError:
            self._drop(client)
            return
        client.read_wants_write = False
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
        if (word not in ("HELLO", "HELLO2") or (self.e2ee_only and word != "HELLO2") or (own, peer) not in self.allowed or
                (self.tls_context is not None and client.auth_device != own)):
            self._drop(client)
            return False
        version = 2 if word == "HELLO2" else 1
        previous = self.by_device.get(own)
        if previous is not None:
            if previous.peer != peer or previous.version != version:
                self._drop(client)
                return False
            # A device reset may never send FIN/RST. The latest valid
            # registration replaces its old transport, not its durable state.
            self._drop(previous)
        client.version = version
        client.device = own
        client.peer = peer
        self.by_device[own] = client
        self._redeliver(own)
        return True

    def _consume_frames(self, client):
        while client.sock in self.clients and len(client.input) >= 4:
            size = struct.unpack(">I", client.input[:4])[0]
            if size < (MAIL_HEADER.size if client.version == 2 else HEADER.size) or size > MAX_FRAME:
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
        if len(frame) >= 3 and frame[2] == 2:
            if len(frame) < MAIL_HEADER.size:
                raise ValueError("short encrypted frame")
            fields = MAIL_HEADER.unpack(frame[:MAIL_HEADER.size])
            magic, version, kind, source, destination, mid, length = fields
            envelope = frame[MAIL_HEADER.size:]
            if (magic != b"FL" or kind not in (4, 5) or
                    (source, destination) not in self.allowed or
                    not 134 <= length <= 256 or len(envelope) != length or
                    envelope[:5] != b"FAE\x01\x01" or envelope[5] > 122 or
                    length != 134 + envelope[5] or envelope[70:86] != mid or
                    (kind == 4 and length < 139) or (kind == 5 and length != 172)):
                raise ValueError("invalid encrypted frame")
            return fields
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

        # Do not retain waiting keys for legacy records that have been retired.
        # Otherwise repeated ACK/new-message traffic could grow this queue while
        # a slow recipient's output stays full.
        client.deliveries.pop(match["key"], None)
        sender = self.by_device.get(destination)
        if sender is not None:
            self._queue_frame(sender, frame)

    def _process_frame(self, client, frame):
        try:
            fields = self._parse_frame(frame)
        except (ValueError, struct.error):
            self._drop(client)
            return
        if client.device != fields[3] or frame[2] != client.version:
            self._drop(client)
            return
        if frame[2] == 2:
            self._process_encrypted(frame)
        elif fields[2] == 3:
            self._process_device_ack(client, fields, frame)
        else:
            self._process_original(client, frame, fields)

    def _process_encrypted(self, frame):
        record = self._record_from_frame(frame)
        pending = self._find_pending(record["key"])
        if pending is not None:
            # IDs cannot silently replace a previously accepted ciphertext.
            if pending["frame"] != record["frame"]:
                return
        else:
            if len(self.state["pending"]) >= self.max_pending:
                return  # sender retains its durable outbox and retries
            previous = copy.deepcopy(self.state)
            self.state["pending"].append(record)
            try:
                self._save()
            except PersistenceError:
                self.state = previous
                raise
        self._deliver(record)
        if record["kind"] == 4:
            key = f"e:{record['destination']}:{record['event_id']}:5"
            receipt = self._find_pending(key)
            if receipt is not None:
                self._deliver(receipt)

    def _deliver(self, record):
        recipient = self.by_device.get(record["destination"])
        if recipient is not None and recipient.version == record.get("version", 1):
            recipient.deliveries.setdefault(record["key"], None)
            self._fill_deliveries(recipient)

    def _redeliver(self, device):
        for record in self.state["pending"]:
            if record["destination"] == device:
                self._deliver(record)

    def run(self):
        while True:
            self._expire_clients()
            # SSL may hold decrypted records even when the fd is not readable.
            buffered = [c for c in self.clients.values()
                        if self.tls_context is not None and not c.tls_handshake
                        and c.sock.pending()]
            for client in buffered:
                if client.sock in self.clients:
                    self._read(client)
            for key, events in self.selector.select(timeout=0 if buffered else 0.5):
                if key.fileobj is self.server:
                    self._accept()
                    continue
                client = key.data
                if client.sock not in self.clients:
                    continue
                if client.tls_handshake:
                    self._tls_handshake(client)
                    continue
                if client.write_wants_read and events & selectors.EVENT_READ:
                    self._write(client)
                if client.sock not in self.clients:
                    continue
                if events & selectors.EVENT_READ or (client.read_wants_write and events & selectors.EVENT_WRITE):
                    self._read(client)
                if events & selectors.EVENT_WRITE and client.sock in self.clients:
                    self._write(client)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--e2ee-only", action="store_true", help="reject legacy plaintext clients")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=7777)
    parser.add_argument("--data", default=".falafel-relay/state.json")
    parser.add_argument("--pair", default="1:2")
    parser.add_argument("--max-pending", type=int, default=512)
    parser.add_argument("--max-completed", type=int, default=1024)
    parser.add_argument("--tls-cert")
    parser.add_argument("--tls-key")
    parser.add_argument("--tls-ca")
    parser.add_argument("--tls-devices", help="JSON SHA-256 leaf certificate fingerprint -> authorized device ID")
    parser.add_argument("--handshake-timeout", type=float, default=10.0)
    parser.add_argument("--max-clients", type=int, default=64)
    parser.add_argument("--max-pending-connections", type=int, default=16)
    args = parser.parse_args()
    tls_args = (args.tls_cert, args.tls_key, args.tls_ca, args.tls_devices)
    context, devices = None, None
    if any(value is not None for value in tls_args):
        if not all(tls_args):
            parser.error("mTLS requires --tls-cert, --tls-key, --tls-ca and --tls-devices; no TCP fallback")
        try:
            try:
                from .tls_auth import server_context, load_devices
            except ImportError:
                from tls_auth import server_context, load_devices
            context = server_context(args.tls_cert, args.tls_key, args.tls_ca)
            devices = load_devices(args.tls_devices)
        except (OSError, ValueError, ssl.SSLError):
            parser.error("cannot load mTLS credentials or authorization map")
    if (not 0 < args.handshake_timeout <= 300 or args.max_clients <= 0 or
            args.max_pending_connections <= 0):
        parser.error("invalid handshake timeout or connection limits")
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
          args.max_pending, args.max_completed, e2ee_only=args.e2ee_only,
          tls_context=context, tls_devices=devices, handshake_timeout=args.handshake_timeout,
          max_clients=args.max_clients, max_pending_connections=args.max_pending_connections).run()


if __name__ == "__main__":
    main()
