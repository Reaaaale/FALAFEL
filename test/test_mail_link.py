#!/usr/bin/env python3
"""Actual independent C peer and Python relay processes; no crypto mocks."""
import glob
import hashlib
import json
import os
import pathlib
import select
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import unittest

PEER = sys.argv.pop(1)
RELAY = str(pathlib.Path(__file__).resolve().parents[1] / 'relay/falafel_relay.py')
HEADER = struct.Struct('>2sBBII16sH')
SECRET = 'FALAFEL_PRIVATE_PAYLOAD_3C2C'

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

def wait_for(test, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if test():
            return
        time.sleep(.01)
    raise AssertionError('condition timed out')

class Proxy:
    """Real byte-fragmenting proxy, disconnecting the first partial upload."""
    def __init__(self, target):
        self.target = target
        self.server = socket.socket()
        self.server.bind(('127.0.0.1', 0))
        self.server.listen()
        self.server.settimeout(.1)
        self.port = self.server.getsockname()[1]
        self.stop = False
        self.dropped = False
        self.error = None
        self.thread = threading.Thread(target=self.run)
        self.thread.start()
    def run(self):
        try:
            while not self.stop:
                try:
                    client, _ = self.server.accept()
                except socket.timeout:
                    continue
                with client, socket.create_connection(('127.0.0.1', self.target)) as upstream:
                    uploaded = 0
                    while not self.stop:
                        ready, _, _ = select.select([client, upstream], [], [], .05)
                        done = False
                        for source in ready:
                            data = source.recv(7)
                            if not data:
                                done = True
                                break
                            if source is client:
                                uploaded += len(data)
                                if not self.dropped and uploaded > 50:
                                    self.dropped = True
                                    done = True
                                    break
                            destination = upstream if source is client else client
                            for i in range(0, len(data), 3):
                                destination.sendall(data[i:i+3])
                        if done:
                            break
        except (ConnectionError, OSError) as error:
            if not self.stop:
                self.error = error
    def close(self):
        self.stop = True
        self.thread.join(3)
        self.server.close()
        assert not self.thread.is_alive()

class Integration(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.port = port()
        self.children = []
        self.log = open(self.root / 'relay.log', 'wb')
        subprocess.run([PEER, 'provision', str(self.root)], check=True, capture_output=True)
        self.relay = None
        self.start_relay()
    def tearDown(self):
        for child in self.children:
            if child.poll() is None:
                child.terminate()
            child.communicate(timeout=5)
        self.stop_relay()
        self.log.close()
        self.temp.cleanup()
    def start_relay(self, capacity=512):
        self.relay = subprocess.Popen([sys.executable, RELAY, '--port', str(self.port),
            '--data', str(self.root/'relay.json'), '--e2ee-only', '--max-pending', str(capacity)],
            stdout=self.log, stderr=self.log)
        def started():
            if self.relay.poll() is not None:
                raise AssertionError('relay failed startup')
            try:
                with socket.create_connection(('127.0.0.1', self.port), .1):
                    return True
            except OSError:
                return False
        wait_for(started)
    def stop_relay(self):
        if self.relay is not None:
            self.relay.terminate()
            self.relay.wait(timeout=5)
            self.relay = None
    def spawn(self, own, ms=500, mode='consume', count=0, endpoint=None, text=SECRET, root=None):
        env = dict(os.environ, FALAFEL_TEST_IO_CHUNK='3')
        child = subprocess.Popen([PEER, 'run', str(root or self.root), str(own), str(endpoint or self.port),
            str(ms), mode, str(count), text], stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
        self.children.append(child)
        return child
    def result(self, child, code=0):
        out, err = child.communicate(timeout=12)
        self.assertEqual(child.returncode, code, (out, err))
        return json.loads(out) if out else None
    def run_peer(self, own, **kw):
        return self.result(self.spawn(own, **kw))
    def records(self):
        try:
            return json.loads((self.root/'relay.json').read_text())['pending']
        except (FileNotFoundError, json.JSONDecodeError):
            return []
    def envelopes(self, own):
        data = (self.root/f'{own}.mail').read_bytes()
        return [data[72+i*517+5:72+i*517+5+int.from_bytes(data[73+i*517:75+i*517], 'big')]
                for i in range(8) if data[72+i*517]]
    def send_raw(self, own, frames):
        with socket.create_connection(('127.0.0.1', self.port)) as s:
            s.sendall(f'HELLO2 {own} {3-own}\n'.encode())
            for f in frames:
                packet = struct.pack('>I', len(f)) + f
                for i in range(0, len(packet), 2):
                    s.sendall(packet[i:i+2])
            time.sleep(.08)
    def frame(self, own, kind, env):
        return HEADER.pack(b'FL', 2, kind, own, 3-own, env[70:86], len(env)) + env
    def assert_ciphertext_only(self):
        self.log.flush()
        paths = [p for p in self.root.iterdir() if not p.name.endswith('.identity')]
        for p in paths:
            if not p.is_file():
                continue
            content = p.read_bytes()
            self.assertNotIn(SECRET.encode(), content, p.name)
            self.assertNotIn(SECRET.encode().hex().encode(), content, p.name)
        for name in ('relay.json', 'relay.log'):
            target = self.root/name
            if target.exists():
                content = target.read_bytes()
                for own in (1,2):
                    private = (self.root/f'{own}.identity').read_bytes()[:32]
                    self.assertNotIn(private, content)
                    self.assertNotIn(private.hex().encode(), content)
        for effect in self.root.glob('2.effect-*'):
            if not effect.name.endswith('.lock'):
                self.assertEqual(effect.read_bytes()[48:], hashlib.sha256(SECRET.encode()).digest())
        for item in self.records():
            frame = bytes.fromhex(item['frame'])
            self.assertEqual(frame[2], 2)
            self.assertEqual(frame[30:35], b'FAE\x01\x01')
    def test_offline_sender_shutdown_relay_restart_and_saved_ack(self):
        first = self.run_peer(1, count=2)
        self.assertEqual(first['out'][:2], [1, 1])
        original = self.envelopes(1)[0]
        wait_for(lambda: len(self.records()) == 2)
        self.assertEqual(bytes.fromhex(self.records()[0]['frame'])[30:], original)
        # A has exited before B ever connects. Restart durable relay with pending data.
        self.stop_relay(); self.start_relay()
        b = self.run_peer(2)
        self.assertEqual(b['in'][:2], [2, 2])
        wait_for(lambda: len(self.records()) == 4)
        # Persist receipts in reverse order before relay restart: A's first
        # completion events must arrive out of the original enqueue order.
        self.stop_relay()
        state = json.loads((self.root/'relay.json').read_text())
        state['pending'].reverse()
        (self.root/'relay.json').write_text(json.dumps(state))
        self.start_relay()
        # B also exits; A must authenticate the relay's saved *peer* receipts.
        a = self.run_peer(1)
        self.assertEqual(a['out'][:2], [2, 2])
        self.assertEqual(self.envelopes(1)[0], original)
        again = self.run_peer(2)
        self.assertEqual(again['in'].count(2), 2)
        self.assertEqual(len(glob.glob(str(self.root/'2.effect-*')))//2, 2)  # data + lock
        self.assert_ciphertext_only()
    def test_online_fragmented_disconnect_retry_and_duplicate_receipts(self):
        proxy = Proxy(self.port)
        try:
            b = self.spawn(2, ms=3200)
            a = self.spawn(1, ms=2800, count=2, endpoint=proxy.port)
            ar = self.result(a); br = self.result(b)
            self.assertTrue(proxy.dropped)
            self.assertIsNone(proxy.error)
            self.assertEqual(ar['out'][:2], [2, 2])
            self.assertEqual(br['in'][:2], [2, 2])
        finally:
            proxy.close()
        records = self.records()
        data = [bytes.fromhex(r['frame']) for r in records if r['kind'] == 4]
        self.assertEqual([f[30:] for f in data], self.envelopes(1))
        receipts = [bytes.fromhex(r['frame']) for r in records if r['kind'] == 5]
        # Separate B socket replays valid ACKs in reverse order, twice, to restarted A.
        a = self.spawn(1, ms=500)
        time.sleep(.08)
        self.send_raw(2, receipts[::-1] * 2)
        ar = self.result(a)
        self.assertEqual(ar['out'][:2], [2, 2])
        self.assertGreaterEqual(ar['duplicates'], 2)
        self.assert_ciphertext_only()
    def test_invalid_payload_unpaired_sender_and_false_ack(self):
        self.run_peer(1, ms=0, count=2)
        envs = self.envelopes(1)
        altered = bytearray(envs[0]); altered[-1] ^= 1
        unpaired = bytearray(envs[1]); unpaired[6] ^= 1
        b = self.spawn(2, ms=450)
        time.sleep(.06)
        self.send_raw(1, [self.frame(1,4,altered), self.frame(1,4,unpaired)])
        br = self.result(b)
        self.assertGreaterEqual(br['rejected'], 2)
        self.assertEqual(br['in'], [0]*8)
        # Structurally valid but forged reverse Noise envelope.
        fake = bytearray(172); fake[:6] = b'FAE\x01\x01\x26'
        fake[6:38] = envs[0][38:70]; fake[38:70] = envs[0][6:38]; fake[70:86] = envs[0][70:86]
        self.send_raw(2,[self.frame(2,5,fake)])
        ar = self.run_peer(1, ms=400)
        self.assertEqual(ar['out'][:2], [1,1])
        self.assertGreaterEqual(ar['rejected'], 1)
        self.assert_ciphertext_only()
    def test_full_mailbox_and_consumer_restart_before_consume(self):
        empty_sender = (self.root/'1.mail').read_bytes()
        a = self.spawn(1, ms=650, count=9)
        b = self.spawn(2, ms=650, mode='crash')
        self.result(b, 23)
        ar = self.result(a)
        self.assertEqual(ar['full'], 1)  # ninth enqueue explicitly rejected
        effects_before = set(self.root.glob('2.effect-*'))
        self.assertEqual(len(effects_before), 2)
        br = self.run_peer(2, ms=650)
        self.assertEqual(br['in'].count(2), 8)
        self.assertEqual(len(list(self.root.glob('2.effect-*'))), 16)
        # Receipt and history survive, even at lifetime capacity.
        ar = self.run_peer(1)
        self.assertEqual(ar['out'], [2]*8)
        # A ninth valid ID from an independent fixture cannot displace inbox history.
        extra = self.root/'extra'; extra.mkdir()
        (extra/'1.identity').write_bytes((self.root/'1.identity').read_bytes())
        (extra/'1.mail').write_bytes(empty_sender)
        b = self.spawn(2, ms=500)
        ninth = self.result(self.spawn(1, ms=450, count=1, root=extra))
        br = self.result(b)
        self.assertEqual(ninth['out'][0], 1)
        self.assertGreaterEqual(br['full'], 1)
        self.assertEqual(br['in'], [2]*8)
        self.assertEqual(len(self.envelopes(1)), 8)
        self.assert_ciphertext_only()
    def test_relay_backpressure_and_strict_framing(self):
        self.stop_relay(); self.start_relay(capacity=1)
        ar = self.run_peer(1, count=2)
        self.assertEqual(ar['out'][:2], [1,1])
        self.assertEqual(len(self.records()), 1)
        # E2EE-only server rejects plaintext registration and malformed lengths.
        with socket.create_connection(('127.0.0.1',self.port)) as s:
            s.settimeout(1); s.sendall(b'HELLO 1 2\n'); self.assertEqual(s.recv(1),b'')
        with socket.create_connection(('127.0.0.1',self.port)) as s:
            s.settimeout(1); s.sendall(b'HELLO2 1 2\n'+struct.pack('>I',9999)); self.assertEqual(s.recv(1),b'')
        # A malformed frame closes the connection and must not process a
        # concatenated valid frame buffered behind it.
        original = self.envelopes(1)[1]
        valid = self.frame(1,4,original)
        malformed = bytearray(valid); malformed[3] = 99
        with socket.create_connection(('127.0.0.1',self.port)) as s:
            s.settimeout(1)
            s.sendall(b'HELLO2 1 2\n' + struct.pack('>I',len(malformed)) + malformed +
                      struct.pack('>I',len(valid)) + valid)
            self.assertEqual(s.recv(1), b'')
        self.assertIsNone(self.relay.poll())
        # Once capacity is restored, retained outbox bytes recover delivery.
        self.stop_relay(); self.start_relay()
        b = self.spawn(2,ms=650); a = self.spawn(1,ms=650)
        self.assertEqual(self.result(a)['out'][:2],[2,2]); self.assertEqual(self.result(b)['in'][:2],[2,2])
        self.assert_ciphertext_only()

if __name__ == '__main__':
    unittest.main()
