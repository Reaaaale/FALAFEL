#!/usr/bin/env python3
import os, socket, struct, subprocess, sys, tempfile, time, unittest
HEADER=struct.Struct(">2sBBIIQQQBBH")
def event(kind,source,destination,event_id,payload=b"",revision=0,personality=0,ack=0):
    return HEADER.pack(b"FL",1,kind,source,destination,event_id,1,revision,personality,ack,len(payload))+payload
def send_frame(sock,frame):sock.sendall(struct.pack(">I",len(frame))+frame)
def receive(sock):
    head=sock.recv(4)
    if len(head)!=4:raise EOFError
    size=struct.unpack(">I",head)[0];data=b""
    while len(data)<size:data+=sock.recv(size-len(data))
    return HEADER.unpack(data[:40]),data[40:]
class RelayTest(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();probe=socket.socket();probe.bind(("127.0.0.1",0));self.port=probe.getsockname()[1];probe.close();self.start()
    def start(self):
        self.proc=subprocess.Popen([sys.executable,"relay/falafel_relay.py","--port",str(self.port),"--data",os.path.join(self.tmp.name,"state.json")],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        deadline=time.time()+3
        while time.time()<deadline:
            try:self.a=self.connect(1,2);return
            except OSError:time.sleep(.03)
        self.fail("relay did not start")
    def connect(self,own,peer):
        s=socket.create_connection(("127.0.0.1",self.port),timeout=1);s.settimeout(2);s.sendall(f"HELLO {own} {peer}\n".encode());return s
    def stop(self):
        if self.proc.poll() is None:self.proc.terminate();self.proc.wait(timeout=3)
    def tearDown(self):
        self.a.close();self.stop();self.tmp.cleanup()
    def test_online_ack_and_offline_restart(self):
        b=self.connect(2,1);msg=event(1,1,2,10,b"ciao");send_frame(self.a,msg)
        ack,_=receive(self.a);self.assertEqual(ack[3],0xFFFFFFFF);self.assertEqual(ack[9],1)
        delivered,payload=receive(b);self.assertEqual(payload,b"ciao");self.assertEqual(delivered[5],10)
        send_frame(b,event(3,2,1,10,ack=2));device_ack,_=receive(self.a);self.assertEqual(device_ack[9],2)
        b.close();send_frame(self.a,event(1,1,2,11,b"offline"));self.assertEqual(receive(self.a)[0][9],1)
        send_frame(self.a,event(2,1,2,12,revision=7,personality=2));self.assertEqual(receive(self.a)[0][9],1)
        time.sleep(.1);self.a.close();self.stop();self.start();b=self.connect(2,1)
        got=[receive(b),receive(b)];self.assertEqual({x[0][2] for x in got},{1,2});self.assertIn(b"offline",[x[1] for x in got]);b.close()
if __name__=="__main__":unittest.main()
