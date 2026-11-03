#!/usr/bin/env python3
"""Real controller threads and two SDL/LVGL processes over the existing relay."""
import json
import os
import pathlib
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest

SIM, FIXTURE, PROBE, MODE = sys.argv[1:5]
del sys.argv[1:5]
RELAY = pathlib.Path(__file__).resolve().parents[1] / 'relay/falafel_relay.py'


def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def mailbox_states(data, incoming):
    start=(88 if data[4]==2 else 72)+(8*517 if incoming else 0)
    return [data[start+i*517] for i in range(8)]


def wait(test, seconds=12):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        if test():
            return
        time.sleep(.02)
    raise AssertionError('condition timed out')


class Base(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.temp.name)
        self.children, self.files = [], []
        self.port = port()
        subprocess.run([FIXTURE, 'provision', str(self.root)], check=True, capture_output=True)
        self.relay = None

    def tearDown(self):
        for child in reversed(self.children):
            if child.poll() is None:
                child.terminate()
            try:
                child.wait(timeout=8)
            except subprocess.TimeoutExpired:
                child.kill()
                child.wait()
                self.fail('process failed graceful shutdown')
        for f in self.files:
            f.close()
        self.temp.cleanup()

    def spawn(self, name, command, env=None):
        path = self.root / (name + '.log')
        f = open(path, 'wb')
        self.files.append(f)
        child = subprocess.Popen(command, stdout=f, stderr=subprocess.STDOUT, env=env)
        self.children.append(child)
        return child, path

    def start_relay(self):
        self.relay, path = self.spawn('relay-' + str(len(self.children)), [sys.executable,
            str(RELAY), '--e2ee-only', '--pair', '1:2', '--port', str(self.port),
            '--data', str(self.root / 'relay.json')])

        def connected():
            if self.relay.poll() is not None:
                raise AssertionError(path.read_text())
            try:
                with socket.create_connection(('127.0.0.1', self.port), .1):
                    return True
            except OSError:
                return False
        wait(connected)

    def stop(self, child, expected=0):
        if child.poll() is None:
            child.terminate()
        self.assertEqual(child.wait(timeout=8), expected)

    def probe(self, name, own=1, duration=1200, text='hold', identity=None, mailbox=None, env=None):
        return self.spawn(name, [PROBE, str(own), str(3-own), str(self.port),
            str(identity or self.root / f'{own}.identity'),
            str(mailbox or self.root / f'{own}.mail'), str(duration), text], env)


class Controller(Base):
    def test_missing_corrupt_locked_and_mismatched_inputs(self):
        missing = self.root / 'absent'
        for name, identity, mailbox in [
            ('missing-key', missing, self.root/'1.mail'),
            ('missing-mail', self.root/'1.identity', missing),
            ('wrong-pair', self.root/'2.identity', self.root/'1.mail')]:
            child, log = self.probe(name, identity=identity, mailbox=mailbox)
            self.assertEqual(child.wait(timeout=5), 1, log.read_text())
            self.assertIn('fatal=1', log.read_text())
        self.assertFalse(missing.exists())
        corrupt = self.root/'corrupt.mail'
        corrupt.write_bytes(b'not a mailbox')
        child, log = self.probe('corrupt-mail', mailbox=corrupt)
        self.assertEqual(child.wait(timeout=5), 1, log.read_text())
        self.assertEqual(corrupt.read_bytes(), b'not a mailbox')
        bad_key = self.root/'bad.identity'
        bad_key.write_bytes(bytes(64)); bad_key.chmod(0o600)
        child, log = self.probe('bad-key', identity=bad_key)
        self.assertEqual(child.wait(timeout=5), 1, log.read_text())
        first, log = self.probe('owner', duration=10000)
        wait(lambda: 'EVENT 0' in log.read_text())
        second, log2 = self.probe('contender')
        self.assertEqual(second.wait(timeout=5), 1, log2.read_text())
        self.stop(first)

    def test_queue_backpressure_and_shutdown_while_full(self):
        child, log = self.probe('flood', duration=1600, text='flood')
        self.assertEqual(child.wait(timeout=8), 0, log.read_text())
        content = log.read_text()
        accepted, rejected = map(int, re.search(r'accepted=(\d+) rejected=(\d+)', content).groups())
        self.assertGreater(accepted, 32)
        self.assertGreater(rejected, 0)
        # STORED is 2; errors are 5, not RECEIVED (3) or DELIVERED (4).
        terminal = re.findall(r'EVENT [25] request=([1-9]\d*)', content)
        self.assertEqual(len(terminal), accepted, content)
        self.assertEqual(len(set(terminal)), accepted)
        self.assertEqual(content.count('EVENT 2 '), accepted)
        self.assertNotIn('fatal=1', content)
        active=(self.root/'1.mail').read_bytes()
        self.assertEqual(sum(state==1 for state in mailbox_states(active,False)),8)
        child, log = self.probe('stop-full', duration=1000, text='stop-full')
        self.assertEqual(child.wait(timeout=8), 0, log.read_text())

    def test_runtime_storage_failure_is_fatal(self):
        folder = self.root/'store'; folder.mkdir()
        shutil.copyfile(self.root/'1.mail', folder/'1.mail')
        original = (folder/'1.mail').read_bytes()
        env = dict(os.environ, FALAFEL_PROBE_SEND_DELAY_MS='500')
        child, log = self.probe('storage-fail', duration=4000, text='test',
            mailbox=folder/'1.mail', env=env)
        wait(lambda: 'EVENT 0' in log.read_text())
        original=(folder/'1.mail').read_bytes()  # After explicit v1 -> v2 migration.
        folder.rename(self.root/'moved')
        self.assertEqual(child.wait(timeout=6), 1, log.read_text())
        self.assertIn('error=2 fatal=1', log.read_text())
        self.assertEqual((self.root/'moved'/'1.mail').read_bytes(), original)
        self.assertFalse(folder.exists())

    def test_received_text_and_outgoing_history_restore_after_consumption(self):
        self.start_relay()
        b, blog = self.probe('b', own=2, duration=2500, text='secret-controller')
        a, alog = self.probe('a', duration=2200, text='secret-controller')
        self.assertEqual(a.wait(timeout=8), 0, alog.read_text())
        self.assertEqual(b.wait(timeout=8), 0, blog.read_text())
        for log in (alog, blog):
            self.assertEqual(log.read_text().count('EVENT 3 '), 1, log.read_text())
            self.assertRegex(log.read_text(), r'EVENT 3 .*text_match=1')
            self.assertEqual(log.read_text().count('EVENT 4 '), 1)
        for own in (1, 2):
            data = (self.root/f'{own}.mail').read_bytes()
            self.assertEqual(mailbox_states(data,True)[0], 0)  # Retired only after durable history.
            self.assertNotIn(b'secret-controller', data)
        restored, log = self.probe('restore', own=2, duration=800)
        self.assertEqual(restored.wait(timeout=5), 0, log.read_text())
        self.assertEqual(log.read_text().count('EVENT 6 '), 2)
        self.assertRegex(log.read_text(), r'EVENT 6 .*restored=1')

    def test_explicit_legacy_migration_and_missing_archive_guard(self):
        self.start_relay()
        empty_v1=(self.root/'1.mail').read_bytes()
        for own,count in ((1,8),(2,0)):
            run=subprocess.run([FIXTURE,'run',str(self.root),str(own),str(self.port),'1200','consume',str(count),'legacy-private'],
                capture_output=True,timeout=8)
            self.assertEqual(run.returncode,0,run.stderr)
        self.assertEqual((self.root/'1.mail').read_bytes()[4],1)
        a,alog=self.probe('migrate-a',duration=2500,text='after-upgrade')
        b,blog=self.probe('migrate-b',own=2,duration=2500)
        self.assertEqual(a.wait(timeout=8),0,alog.read_text())
        self.assertEqual(b.wait(timeout=8),0,blog.read_text())
        for own in (1,2):
            self.assertEqual((self.root/f'{own}.mail').read_bytes()[4],2)
            self.assertEqual(len(list((self.root/f'{own}.mail.history').glob('*.msg'))),9)
        other=self.root/'other.mail'; other.write_bytes(empty_v1)
        extra,extra_log=self.probe('other-archive',mailbox=other,duration=400)
        self.assertEqual(extra.wait(timeout=5),0,extra_log.read_text())
        wrong,wrong_log=self.probe('wrong-archive',duration=800,
            env=dict(os.environ,FALAFEL_PROBE_HISTORY=str(other)+'.history'))
        self.assertEqual(wrong.wait(timeout=5),1,wrong_log.read_text())
        # --init-history must not reset an archive required by an upgraded mailbox.
        directory=self.root/'1.mail.history'
        directory.rename(self.root/'saved-history')
        failed,log=self.probe('lost-archive',duration=800)
        self.assertEqual(failed.wait(timeout=5),1,log.read_text())
        self.assertFalse(directory.exists())
        (self.root/'saved-history').rename(directory)
        index=directory/'index'; saved=index.read_bytes()
        index.write_bytes(saved[:-1]+bytes([saved[-1]^1]))
        failed,log=self.probe('corrupt-archive',duration=800)
        self.assertEqual(failed.wait(timeout=5),1,log.read_text())
        self.assertEqual(index.read_bytes(),saved[:-1]+bytes([saved[-1]^1]))


class Gui(Base):
    def simulator(self, name, own, send=False):
        args = [SIM, '--device', str(own), '--peer', str(3-own), '--port', str(self.port),
            '--identity', str(self.root/f'{own}.identity'), '--mailbox', str(self.root/f'{own}.mail'), '--init-history']
        if send:
            args.append('--test-send-preset')
        return self.spawn(name, args)

    def test_two_windows_offline_restart_reconnect_and_no_duplicates(self):
        self.start_relay()
        a, alog = self.simulator('a-offline-send', 1, send=True)
        wait(lambda: '[E2EE] STORED ' in alog.read_text())
        self.assertNotIn('[E2EE] DELIVERED', alog.read_text())
        def relay_has_data():
            try:
                return any(r['kind'] == 4 for r in json.loads((self.root/'relay.json').read_text())['pending'])
            except (FileNotFoundError, json.JSONDecodeError):
                return False
        wait(relay_has_data)
        self.stop(a)  # Durable outbox must survive loss of the sender process.
        self.stop(self.relay, expected=-15); self.start_relay()
        b, blog = self.simulator('b-receive', 2)
        wait(lambda: '[E2EE] RECEIVED ' in blog.read_text())
        def relay_has_receipt():
            try:
                return any(r['kind'] == 5 for r in json.loads((self.root/'relay.json').read_text())['pending'])
            except (FileNotFoundError, json.JSONDecodeError):
                return False
        wait(relay_has_receipt)
        wait(lambda: mailbox_states((self.root/'2.mail').read_bytes(),True)[0]==0)
        self.stop(b)
        a, alog2 = self.simulator('a-recover-ack', 1)
        wait(lambda: '[E2EE] DELIVERED ' in alog2.read_text())  # B is offline.
        b, blog2 = self.simulator('b-restore-and-send', 2, send=True)
        wait(lambda: '[E2EE] RECEIVED ' in alog2.read_text() and '[E2EE] DELIVERED ' in blog2.read_text())
        self.assertRegex(blog2.read_text(), r'HISTORY .*restored=1 sent=0')
        self.stop(self.relay, expected=-15)
        wait(lambda: '[E2EE] CONNECTION 0' in alog2.read_text() and '[E2EE] CONNECTION 0' in blog2.read_text())
        self.start_relay()
        wait(lambda: alog2.read_text().count('[E2EE] CONNECTION 1') >= 2 and
             blog2.read_text().count('[E2EE] CONNECTION 1') >= 2)
        time.sleep(1.2)
        for log in (alog2, blog2):
            self.assertEqual(log.read_text().count('[E2EE] RECEIVED '), 1 if log==alog2 else 0, log.read_text())
            self.assertEqual(log.read_text().count('[E2EE] DELIVERED '), 1, log.read_text())
        self.stop(a); self.stop(b)
        for log in (alog, blog, alog2, blog2):
            content = log.read_text()
            loops, gap = map(int, re.search(r'HEALTH loops=(\d+) max_gap_ms=(\d+)', content).groups())
            self.assertGreater(loops, 10 if log in (alog2, blog2) else 1)
            self.assertLess(gap, 500, content)
            self.assertNotIn('AddressSanitizer', content)
            self.assertNotIn('runtime error:', content)
            print(log.name, 'LVGL loops=', loops, 'max gap ms=', gap)
        self.assertIn('[UI] PERSONALITY local=1', blog2.read_text())
        for name in ('1.mail', '2.mail', 'relay.json'):
            self.assertNotIn(b'Sto arrivando', (self.root/name).read_bytes())
        # Relay stores only existing v2 ciphertext frames, no v1 fallback.
        for item in json.loads((self.root/'relay.json').read_text())['pending']:
            self.assertEqual(bytes.fromhex(item['frame'])[2], 2)

    def test_100_each_direction_history_read_delivery_and_large_replay(self):
        self.start_relay()
        def launch(name, own, send):
            args=[SIM,'--device',str(own),'--peer',str(3-own),'--port',str(self.port),
                '--identity',str(self.root/f'{own}.identity'),'--mailbox',str(self.root/f'{own}.mail'),'--init-history']
            if send: args += ['--test-send-preset','--test-send-count','100']
            return self.spawn(name,args)
        a,alog=launch('hundred-a',1,True)
        b,blog=launch('hundred-b',2,True)
        for child, log in ((a,alog),(b,blog)):
            def complete():
                if child.poll() is not None: raise AssertionError(log.read_text())
                text=log.read_text()
                return text.count('[E2EE] STORED ')==100 and text.count('[E2EE] DELIVERED ')==100 and text.count('[E2EE] RECEIVED ')==100
            wait(complete,seconds=60)
        # A replay contains far more than the adapter's 4096-byte input buffer.
        self.stop(self.relay,expected=-15)
        wait(lambda: '[E2EE] CONNECTION 0' in alog.read_text() and '[E2EE] CONNECTION 0' in blog.read_text())
        self.start_relay()
        wait(lambda: alog.read_text().count('[E2EE] CONNECTION 1')>=2 and blog.read_text().count('[E2EE] CONNECTION 1')>=2)
        time.sleep(1)
        for log in (alog,blog):
            wait(lambda: len(re.findall(r'HISTORY .*sent=0 delivery=2 read=1',log.read_text()))>=100)
        self.stop(a); self.stop(b)
        for log in (alog,blog):
            text=log.read_text()
            self.assertEqual(text.count('[E2EE] RECEIVED '),100,text[-3000:])
            self.assertIn('rows=200',text)
            self.assertNotIn('fatal=1',text)
            print(log.name,re.search(r'\[UI\] HEALTH.*',text).group())
        a,alog=launch('restored-a',1,False)
        b,blog=launch('restored-b',2,False)
        for child,log in ((a,alog),(b,blog)):
            wait(lambda: log.read_text().count('[E2EE] HISTORY ')>=200,seconds=30)
            text=log.read_text()
            self.assertEqual(len(re.findall(r'HISTORY .*restored=1 sent=1 delivery=2 read=1',text)),100)
            self.assertEqual(len(re.findall(r'HISTORY .*restored=1 sent=0 delivery=2 read=1',text)),100)
            self.assertNotIn('[E2EE] RECEIVED ',text)
        self.stop(a); self.stop(b)
        for own in (1,2):
            directory=self.root/f'{own}.mail.history'
            records=list(directory.glob('*.msg'))
            self.assertEqual(len(records),200)
            self.assertEqual((directory/'index').stat().st_size,60+17*200)
            for path in records:
                self.assertEqual(path.stat().st_size,800)
                self.assertNotIn(b'Sto arrivando',path.read_bytes())
            self.assertNotIn(b'Sto arrivando',(self.root/f'{own}.mail').read_bytes())
        print('100 each direction: 200 encrypted records/device, 163460 history bytes/device')

    def test_window_close_and_missing_credentials(self):
        args = [SIM, '--device', '1', '--peer', '2']
        missing, log = self.spawn('no-credentials', args)
        self.assertEqual(missing.wait(timeout=5), 1, log.read_text())
        child, log = self.spawn('window-close', args + ['--identity', str(self.root/'1.identity'),
            '--mailbox', str(self.root/'1.mail'), '--init-history', '--test-duration-ms', '400'])
        self.assertEqual(child.wait(timeout=8), 0, log.read_text())
        self.assertIn('[UI] HEALTH', log.read_text())


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(Controller if MODE == 'controller' else Gui)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(not result.wasSuccessful())
