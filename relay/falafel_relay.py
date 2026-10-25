#!/usr/bin/env python3
"""Local development relay. Plain TCP; intentionally not production secure."""
import argparse, json, os, selectors, socket, struct, tempfile

MAX_FRAME = 296
HEADER = struct.Struct(">2sBBIIQQQBBH")
RELAY_ID = 0xFFFFFFFF

class Relay:
    def __init__(self, host, port, data_file, allowed, max_pending):
        self.selector = selectors.DefaultSelector(); self.clients = {}; self.buffers = {}
        self.data_file = data_file; self.allowed = set(allowed); self.max_pending = max_pending
        self.state = {"pending": [], "seen": []}; self._load()
        self.server = socket.socket(); self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind((host, port)); self.server.listen(); self.server.setblocking(False)
        self.selector.register(self.server, selectors.EVENT_READ, self._accept)
        print(f"Falafel relay listening on {host}:{self.server.getsockname()[1]}", flush=True)

    def _load(self):
        try:
            with open(self.data_file, encoding="utf-8") as f: value = json.load(f)
            if isinstance(value.get("pending"), list) and isinstance(value.get("seen"), list): self.state = value
        except (OSError, ValueError, AttributeError): pass

    def _save(self):
        os.makedirs(os.path.dirname(os.path.abspath(self.data_file)), exist_ok=True)
        fd, name = tempfile.mkstemp(dir=os.path.dirname(os.path.abspath(self.data_file)), prefix=".relay-")
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                json.dump(self.state, f); f.flush(); os.fsync(f.fileno())
            os.replace(name, self.data_file)
        finally:
            try: os.unlink(name)
            except FileNotFoundError: pass

    def _accept(self, sock):
        client, _ = sock.accept(); client.setblocking(False); self.buffers[client] = bytearray()
        self.selector.register(client, selectors.EVENT_READ, self._read)

    def _drop(self, sock):
        for device, current in list(self.clients.items()):
            if current is sock: del self.clients[device]
        self.buffers.pop(sock, None)
        try: self.selector.unregister(sock)
        except Exception: pass
        sock.close()

    def _send(self, sock, frame):
        try: sock.sendall(struct.pack(">I", len(frame)) + frame); return True
        except OSError: self._drop(sock); return False

    def _drain(self, device):
        sock = self.clients.get(device)
        for item in self.state["pending"]:
            frame = bytes.fromhex(item["frame"])
            if item["destination"] == device and sock: self._send(sock, frame)

    def _read(self, sock):
        try: data = sock.recv(4096)
        except OSError: data = b""
        if not data: self._drop(sock); return
        buf = self.buffers[sock]; buf.extend(data)
        if sock not in self.clients.values():
            cut = buf.find(b"\n")
            if cut < 0:
                if len(buf) > 80: self._drop(sock)
                return
            try: word, own, peer = bytes(buf[:cut]).decode("ascii").split(); own, peer = int(own), int(peer)
            except (ValueError, UnicodeError): self._drop(sock); return
            del buf[:cut + 1]
            if word != "HELLO" or (own, peer) not in self.allowed or own in self.clients: self._drop(sock); return
            self.clients[own] = sock; self._drain(own)
        while len(buf) >= 4:
            size = struct.unpack(">I", buf[:4])[0]
            if size < 40 or size > MAX_FRAME: self._drop(sock); return
            if len(buf) < 4 + size: return
            frame = bytes(buf[4:4 + size]); del buf[:4 + size]
            self._frame(sock, frame)

    def _frame(self, sock, frame):
        try: magic, version, kind, source, destination, event_id, timestamp, revision, personality, ack, length = HEADER.unpack(frame[:40])
        except struct.error: self._drop(sock); return
        owner = next((d for d, s in self.clients.items() if s is sock), None)
        if magic != b"FL" or version != 1 or len(frame) != 40 + length or owner != source or (source, destination) not in self.allowed:
            self._drop(sock); return
        if kind not in (1, 2, 3) or (kind == 1 and (length == 0 or ack != 0)) or (kind == 2 and (length != 0 or personality > 2 or revision == 0 or ack != 0)) or (kind == 3 and (length != 0 or ack != 2)):
            self._drop(sock); return
        key = f"{source}:{event_id}:{kind}"
        if kind != 3:
            relay_ack = HEADER.pack(b"FL", 1, 3, RELAY_ID, source, event_id, timestamp, 0, 0, 1, 0)
            self._send(sock, relay_ack)
        else:
            kept = []
            for item in self.state["pending"]:
                original = HEADER.unpack(bytes.fromhex(item["frame"])[:40])
                if not (original[3] == destination and original[4] == source and original[5] == event_id):
                    kept.append(item)
            if kept != self.state["pending"]:
                self.state["pending"] = kept; self._save()
        if key in self.state["seen"]: return
        self.state["seen"] = (self.state["seen"] + [key])[-1024:]
        target = self.clients.get(destination)
        if target and self._send(target, frame): self._save(); return
        if len(self.state["pending"]) >= self.max_pending: self.state["pending"].pop(0)
        if kind == 2:
            self.state["pending"] = [x for x in self.state["pending"] if not (x["destination"] == destination and x["kind"] == 2)]
        self.state["pending"].append({"destination": destination, "kind": kind, "frame": frame.hex()}); self._save()

    def run(self):
        while True:
            for key, _ in self.selector.select(): key.data(key.fileobj)

def main():
    p=argparse.ArgumentParser();p.add_argument("--host",default="127.0.0.1");p.add_argument("--port",type=int,default=7777)
    p.add_argument("--data",default=".falafel-relay/state.json");p.add_argument("--pair",default="1:2")
    p.add_argument("--max-pending",type=int,default=512);a=p.parse_args();x,y=(int(v) for v in a.pair.split(":"))
    if x <= 0 or y <= 0 or x == y or a.max_pending <= 0: p.error("pair IDs and max-pending must be positive and IDs distinct")
    Relay(a.host,a.port,a.data,[(x,y),(y,x)],a.max_pending).run()
if __name__ == "__main__": main()
