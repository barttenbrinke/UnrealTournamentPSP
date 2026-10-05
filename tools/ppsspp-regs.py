#!/usr/bin/env python3
# ppsspp-regs.py [port] -- pause a running PPSSPP (WebSocket debugger), print
# pc/ra of the current thread plus a short stack walk, then resume.
import base64, json, os, socket, struct, sys
port = int(sys.argv[1]) if len(sys.argv) > 1 else 45678
s = socket.create_connection(('127.0.0.1', port), timeout=20)
key = base64.b64encode(os.urandom(16)).decode()
s.sendall(('GET /debugger HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n'
           'Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Protocol: debugger.ppsspp.org\r\n\r\n' % key).encode())
buf = b''
while b'\r\n\r\n' not in buf: buf += s.recv(4096)
buf = buf.split(b'\r\n\r\n', 1)[1]
def send(obj):
    p = json.dumps(obj).encode(); m = os.urandom(4)
    hdr = bytes([0x81]) + (bytes([0x80 | len(p)]) if len(p) < 126 else bytes([0x80 | 126]) + struct.pack('>H', len(p)))
    s.sendall(hdr + m + bytes(b ^ m[i % 4] for i, b in enumerate(p)))
def recv():
    global buf
    def need(n):
        global buf
        while len(buf) < n: buf += s.recv(1 << 20)
    need(2); n = buf[1] & 0x7f; off = 2
    if n == 126: need(4); n = struct.unpack('>H', buf[2:4])[0]; off = 4
    elif n == 127: need(10); n = struct.unpack('>Q', buf[2:10])[0]; off = 10
    need(off + n); p = buf[off:off + n]; buf = buf[off + n:]
    return json.loads(p)
def call(ev, **kw):
    send(dict(event=ev, **kw))
    while True:
        r = recv()
        if r.get('event') in (ev, 'error'): return r
send({'event': 'cpu.stepping'})
while recv().get('event') != 'cpu.stepping': pass
for _ in range(1):
    r = call('cpu.getAllRegs')
    gpr = r['categories'][0]
    regs = dict(zip(gpr['registerNames'], gpr['uintValues']))
    print('pc %08x ra %08x sp %08x' % (regs.get('pc', 0), regs['ra'], regs['sp']))
    st = call('hle.backtrace', thread=None) if False else None
send({'event': 'cpu.resume'})
