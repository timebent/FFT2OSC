#!/usr/bin/env python3
import socket
import struct

HOST = '0.0.0.0'
PORT = 57120

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))
print(f'Listening for OSC on {HOST}:{PORT}')
while True:
    data, addr = sock.recvfrom(65536)
    if b'/fft/amplitudes' not in data:
        continue
    # find address string and align to 4
    try:
        addr_end = data.index(b'\x00', 0) + 1
    except ValueError:
        continue
    # find type tag start (comma)
    try:
        comma = data.index(b',', addr_end)
    except ValueError:
        continue
    # type tags are padded to 4 bytes
    types_end = data.index(b'\x00', comma) + 1
    pad = (4 - (types_end % 4)) % 4
    payload_offset = types_end + pad
    # count number of 'f' in type tags
    types = data[comma+1:types_end].decode('ascii', errors='ignore')
    float_count = types.count('f')
    floats = []
    for i in range(float_count):
        start = payload_offset + i*4
        if start+4 > len(data): break
        f = struct.unpack('!f', data[start:start+4])[0]
        floats.append(f)
    print(f'Received {len(floats)} floats from {addr}, first 8: {floats[:8]}')
