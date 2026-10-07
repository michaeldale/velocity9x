"""Decompress the SZDD (MS COMPRESS / LZExpand) members embedded in a file.

Each member: 'SZDD' 88 F0 27 33, 'A', the missing last character, the
uncompressed size (LE32), then LZSS: a flag byte, low bit first, 1 a
literal, 0 a pair (offset 12 bits, length 4 bits + 3) into a 4096-byte
window filled with spaces, written from position 4096 - 16.
"""
import struct
import sys

MAGIC = b'SZDD\x88\xf0\x27\x33'


def expand(data, start):
    size = struct.unpack_from('<I', data, start + 10)[0]
    pos = start + 14
    window = bytearray(b' ' * 4096)
    wpos = 4096 - 16
    out = bytearray()
    while len(out) < size and pos < len(data):
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= size or pos >= len(data):
                break
            if flags & (1 << bit):
                byte = data[pos]
                pos += 1
                out.append(byte)
                window[wpos] = byte
                wpos = (wpos + 1) & 4095
            else:
                lo = data[pos]
                hi = data[pos + 1]
                pos += 2
                offset = lo | ((hi & 0xF0) << 4)
                length = (hi & 0x0F) + 3
                for k in range(length):
                    byte = window[(offset + k) & 4095]
                    out.append(byte)
                    window[wpos] = byte
                    wpos = (wpos + 1) & 4095
    return bytes(out[:size]), size


def main():
    data = open(sys.argv[1], 'rb').read()
    start = 0
    index = 0
    while True:
        start = data.find(MAGIC, start)
        if start < 0:
            break
        body, size = expand(data, start)
        name = 'member%02d.bin' % index
        open(name, 'wb').write(body)
        kind = 'PE' if body[:2] == b'MZ' else 'text?'
        head = body[:60].decode('latin-1').replace('\r', ' ').replace('\n', ' ')
        print(index, start, size, len(body), kind, repr(head[:50]))
        index += 1
        start += 8


main()
