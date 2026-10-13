"""Exercise the real Windows LAN implementation with loopback UDP/TCP peers.

Run: python tests/test_lan_integration.py PATH_TO_lan_native_test.exe
No GUI, personal saves, firewall changes, or subnet scanning are used.
"""
import hashlib
import pathlib
import queue
import re
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import zlib

HEADER = struct.Struct('<IHHI')
MAGIC = 0x31444F50
HUMAN = struct.Struct('<IIIiii5f32s')
INPUT = struct.Struct('<6f5I')
MOB = struct.Struct('<I7i8f')
SNAPSHOT_COUNT = HUMAN.size * 6 + (8 + 1 + 6 + 6 + 6 + 6 + 6) * 4 + 6 * 32
SNAPSHOT_MOBS = SNAPSHOT_COUNT + 4
SKINS = {}


def native_arrays(payload):
    if payload[:8] == b'PXCDLTZ1':
        blocks, meta = struct.unpack_from('<II', payload, 20)
        return zlib.decompress(payload[28:28+blocks]), zlib.decompress(payload[28+blocks:28+blocks+meta])
    if payload[:8] == b'CHNKDLT2':
        return payload[20:20+65536], payload[20+65536:20+131072]
    raise AssertionError('Unexpected native chunk container')


def drain_stdout(process, lines, collected):
    for line in process.stdout:
        collected.append(line.rstrip())
        lines.put(line.rstrip())


def wait_line(lines, prefix, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        line = lines.get(timeout=max(.01, end-time.monotonic()))
        if line.startswith(prefix):
            return line
    raise AssertionError(f'Missing output: {prefix}')


def receive_exact(connection, count):
    data = bytearray()
    while len(data) < count:
        part = connection.recv(count-len(data))
        if not part:
            raise EOFError('TCP peer closed before completing a frame')
        data.extend(part)
    return bytes(data)


def receive_frame(connection):
    magic, version, kind, length = HEADER.unpack(receive_exact(connection, HEADER.size))
    assert magic == MAGIC and version == 5 and length <= 196608
    payload=receive_exact(connection,length)
    if kind==10:
        identity,width,height,size=struct.unpack_from('<4I',payload)
        assert identity<6 and width==64 and height in (32,64) and size==width*height*4 and len(payload)==16+size
        SKINS.setdefault(connection,{})[identity]=payload[16:]
    return kind,payload


def send_frame(connection, kind, payload, fragmented=False):
    wire = HEADER.pack(MAGIC, 5, kind, len(payload)) + payload
    if fragmented:
        for offset in range(0, len(wire), 3):
            connection.sendall(wire[offset:offset+3])
    else:
        connection.sendall(wire)


def base36(value):
    sign = '-' if value < 0 else ''
    value = abs(value)
    digits = ''
    while value:
        value, digit = divmod(value, 36)
        digits = '0123456789abcdefghijklmnopqrstuvwxyz'[digit] + digits
    return sign + (digits or '0')


def join(port, name, fragmented=False):
    connection = socket.create_connection(('127.0.0.1', port), timeout=5)
    connection.settimeout(5)
    send_frame(connection, 1, name.encode().ljust(32, b'\0'), fragmented)
    chunks = 0
    welcome = None
    while True:
        kind, payload = receive_frame(connection)
        if kind == 2:
            own_id, expected = struct.unpack_from('<II', payload)
            human = HUMAN.unpack_from(payload, 16)
            welcome = (own_id, expected, human)
        elif kind == 3:
            cx, cz, count = struct.unpack_from('<iii', payload, 8)
            assert count == 65536
            file = pathlib.Path('src/assets/maps/nuketown/chunks') / f'c.{base36(cx)}.{base36(cz)}.dat'
            assert native_arrays(payload) == native_arrays(file.read_bytes())
            chunks += 1
        elif kind == 10:
            pass # skin cache is verified separately below
        elif kind == 4:
            assert welcome and chunks == welcome[1] == 625
            break
        else:
            raise AssertionError(f'Unexpected bootstrap frame {kind}')
    _, _, human = welcome
    # Try claiming host identity: the server must assign the socket's identity.
    rgba=bytes((welcome[0]*30,50,90,255))*(64*32)
    send_frame(connection,10,struct.pack('<4I',0,64,32,len(rgba))+rgba)
    send_frame(connection, 5, INPUT.pack(*human[6:11], 1.0, 0, 0, 0, 0, 0))
    return connection


def wait_slots(connection, humans, bots, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        kind, payload = receive_frame(connection)
        if kind != 6:
            continue
        real = sum(HUMAN.unpack_from(payload, i*HUMAN.size)[0] != 0 for i in range(6))
        count = struct.unpack_from('<I', payload, SNAPSHOT_COUNT)[0]
        npc = sum(MOB.unpack_from(payload, SNAPSHOT_MOBS+i*MOB.size)[2] != 0 for i in range(count))
        if (real, npc) == (humans, bots):
            return
    raise AssertionError(f'No authoritative {humans} humans / {bots} bots snapshot')


def snapshot(connection):
    while True:
        kind, payload = receive_frame(connection)
        if kind == 6:
            return payload


def main():
    executable = pathlib.Path(sys.argv[1]).resolve()
    temp_root = pathlib.Path(r'C:\Users\cafestock.ir\AppData\Local\Temp\opencode')
    sandbox = pathlib.Path(tempfile.mkdtemp(prefix='lan_socket_test_', dir=temp_root))
    stop = sandbox / 'host.stop'
    lines, collected = queue.Queue(), []
    host = subprocess.Popen([str(executable), 'host', str(sandbox), str(stop)],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    reader = threading.Thread(target=drain_stdout, args=(host, lines, collected), daemon=True)
    reader.start()
    clients = []
    try:
        ready = wait_line(lines, 'HOST_READY')
        port = int(ready.split()[1])
        print(ready)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as discovery:
            discovery.settimeout(5)
            discovery.sendto(b'POD_DISCOVER', ('127.0.0.1', 27567))
            deadline=time.monotonic()+5
            while True:
                response, address = discovery.recvfrom(192)
                if response.startswith(f'POD_GAME 5 {port} 1 '.encode()):break
                assert time.monotonic()<deadline, response
            print('PASS UDP LAN discovery while host is paused:', response.decode())
        for i in range(5):
            clients.append(join(port, f'Test{i+1}', fragmented=i==0))
            wait_slots(clients[-1], i+2, 0)
            print(f'PASS {i+2} real players in lobby; 625 host map chunks verified')
        assert SKINS[clients[-1]][0]==bytes((180,20,40,255))*(64*64)
        for identity in range(1,5):
            assert SKINS[clients[-1]][identity]==bytes((identity*30,50,90,255))*(64*32)
        updated=bytes((17,123,201,255))*(64*64)
        send_frame(clients[0],10,struct.pack('<4I',5,64,64,len(updated))+updated)
        for _ in range(100):
            receive_frame(clients[-1])
            if SKINS[clients[-1]].get(1)==updated:break
        assert SKINS[clients[-1]][1]==updated
        assert SKINS[clients[-1]][0]==bytes((180,20,40,255))*(64*64)
        assert SKINS[clients[-1]][2]==bytes((60,50,90,255))*(64*32)
        print('PASS host skin, distinct player skins, late-join cache, live skin updates and spoofed identity isolation')
        with socket.create_connection(('127.0.0.1', port), timeout=5) as overflow:
            overflow.settimeout(5)
            kind, message = receive_frame(overflow)
            assert kind == 7 and b'full' in message
            print('PASS seventh real player rejected:', message.decode())
        clients[-1].close()
        clients.pop()
        wait_slots(clients[0], 5, 0)
        print('PASS disconnected human removed from lobby')
        # Malformed movement does not poison simulation: offending peer closes.
        send_frame(clients[-1], 5, INPUT.pack(float('nan'), 65, 0, 0, 0, 0, 0, 0, 0, 0, 0))
        wait_slots(clients[0], 4, 0)
        clients[-1].close()
        clients.pop()
        print('PASS malformed NaN movement rejected and lobby player removed')
        for connection in clients:
            connection.close()
        clients.clear()
        wait_line(lines, 'SLOTS 1 0')
        # Test the real C client as well as independent Python protocol peers.
        client_lines, client_output = queue.Queue(), []
        client = subprocess.Popen([str(executable), 'client', str(sandbox), f'127.0.0.1:{port}', '30'],
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        threading.Thread(target=drain_stdout,args=(client,client_lines,client_output),daemon=True).start()
        ready = wait_line(client_lines,'CLIENT_READY')
        print(ready)
        cache = pathlib.Path(re.search(r'CACHE (.+)', ready).group(1).strip())
        assert len(list((cache/'chunks').glob('*.dat'))) == 625
        for file in (cache/'chunks').glob('*.dat'):
            original = pathlib.Path('src/assets/maps/nuketown/chunks') / file.name
            assert native_arrays(file.read_bytes()) == native_arrays(original.read_bytes())
        print('PASS native C client join/snapshot handling and matching downloaded terrain/metadata')
        observer=join(port,'LobbyObserver');clients.append(observer)
        wait_slots(observer,3,0)
        send_frame(observer,9,struct.pack('<i',1))
        for _ in range(30):
            payload=snapshot(observer)
            phase,countdown,*teams=struct.unpack_from('<8i',payload,HUMAN.size*6)
            if teams[2]==1:break
        assert phase==0 and teams[2]==1
        print('PASS joining player switches teams in the authoritative lobby')
        send_frame(observer,5,INPUT.pack(48,66.62,-85,0,0,1,1,0,0,0,0))
        payload=snapshot(observer)
        assert HUMAN.unpack_from(payload,2*HUMAN.size)[4]==32
        began=time.monotonic();(sandbox/'match.start').write_text('start',encoding='ascii')
        seen=set()
        while True:
            payload=snapshot(observer)
            phase,countdown,*teams=struct.unpack_from('<8i',payload,HUMAN.size*6)
            if phase==1:seen.add(countdown)
            if phase==2:break
        assert time.monotonic()-began>=4.8 and {1,2,3,4,5}<=seen
        wait_slots(observer,3,3)
        count=struct.unpack_from('<I',payload,SNAPSHOT_COUNT)[0]
        bot_teams=[MOB.unpack_from(payload,SNAPSHOT_MOBS+i*MOB.size)[7]
                   for i in range(count) if MOB.unpack_from(payload,SNAPSHOT_MOBS+i*MOB.size)[2]]
        for team in range(2):
            humans=sum(HUMAN.unpack_from(payload,i*HUMAN.size)[0] and teams[i]==team for i in range(6))
            assert humans+bot_teams.count(team)==3
        print('PASS host start, five full countdown seconds and exactly three members per team')
        observer.close();clients.remove(observer)
        (sandbox/'block.change').write_text('change',encoding='ascii')
        try:
            wait_line(client_lines,'CLIENT_BLOCK_UPDATE_SEEN',timeout=5)
        except Exception:
            print('HOST OUTPUT:', collected)
            print('CLIENT OUTPUT:', client_output)
            if client.poll() is not None:print('CLIENT STDERR:',client.stderr.read())
            raise
        (sandbox/'client.stop').write_text('stop',encoding='ascii')
        client.wait(timeout=5)
        assert client.returncode==0,client.stderr.read()
        blocks, meta = native_arrays((cache/'chunks/c.6.6.dat').read_bytes())
        assert blocks[90*256+4*16+4] == 20
        print('PASS live host terrain change reaches native client collision storage and map cache')
    finally:
        for connection in clients:
            connection.close()
        stop.write_text('stop', encoding='ascii')
        try:
            host.wait(timeout=5)
        except subprocess.TimeoutExpired:
            host.terminate()
            host.wait(timeout=5)
    assert host.returncode == 0, host.stderr.read()
    print('PASS native host shutdown and socket cleanup')


if __name__ == '__main__':
    main()
