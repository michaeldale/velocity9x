"""List or extract members of an LHA archive (level 0/1 headers, -lh0- and
-lh5- only), enough for Quake 1.06 shareware's resource.1.

usage: lha.py ARCHIVE            list
       lha.py ARCHIVE NAME OUT   extract the member whose path ends in NAME
"""
import struct
import sys


class Bits:
    def __init__(self, data):
        self.data = data
        self.pos = 0
        self.buf = 0
        self.count = 0

    def fill(self, n):
        while self.count < n:
            byte = self.data[self.pos] if self.pos < len(self.data) else 0
            self.pos += 1
            self.buf = (self.buf << 8) | byte
            self.count += 8

    def peek(self, n):
        self.fill(n)
        return (self.buf >> (self.count - n)) & ((1 << n) - 1)

    def skip(self, n):
        self.fill(n)
        self.count -= n
        self.buf &= (1 << self.count) - 1

    def get(self, n):
        if n == 0:
            return 0
        value = self.peek(n)
        self.skip(n)
        return value


def make_table(lengths):
    """Canonical codes: by length, then symbol. Returns (table, maxlen),
    table indexed by maxlen-bit peek -> (symbol, length)."""
    maxlen = max(lengths) if lengths else 0
    if maxlen == 0:
        return None, 0
    table = [None] * (1 << maxlen)
    code = 0
    for length in range(1, maxlen + 1):
        for symbol, l in enumerate(lengths):
            if l == length:
                start = code << (maxlen - length)
                for k in range(1 << (maxlen - length)):
                    table[start + k] = (symbol, length)
                code += 1
        code <<= 1
    return table, maxlen


class Tree:
    def __init__(self, lengths=None, constant=None):
        self.constant = constant
        if constant is None:
            self.table, self.maxlen = make_table(lengths)

    def decode(self, bits):
        if self.constant is not None:
            return self.constant
        entry = self.table[bits.peek(self.maxlen)]
        if entry is None:
            raise ValueError('bad code')
        bits.skip(entry[1])
        return entry[0]


def read_pt_len(bits, nt, nbit, special):
    n = bits.get(nbit)
    if n == 0:
        return Tree(constant=bits.get(nbit))
    lengths = [0] * nt
    i = 0
    while i < n:
        c = bits.peek(3)
        if c == 7:
            bits.skip(3)
            while bits.get(1) == 1:
                c += 1
        else:
            bits.skip(3)
        lengths[i] = c
        i += 1
        if i == special:
            zeros = bits.get(2)
            for _ in range(zeros):
                lengths[i] = 0
                i += 1
    return Tree(lengths)


def read_c_len(bits, pt):
    n = bits.get(9)
    if n == 0:
        return Tree(constant=bits.get(9))
    lengths = [0] * 510
    i = 0
    while i < n:
        c = pt.decode(bits)
        if c <= 2:
            if c == 0:
                count = 1
            elif c == 1:
                count = bits.get(4) + 3
            else:
                count = bits.get(9) + 20
            i += count
        else:
            lengths[i] = c - 2
            i += 1
    return Tree(lengths)


def lh5(data, size):
    bits = Bits(data)
    out = bytearray()
    remaining = 0
    ctree = ptree = None
    while len(out) < size:
        if remaining == 0:
            remaining = bits.get(16)
            pt = read_pt_len(bits, 19, 5, 3)
            ctree = read_c_len(bits, pt)
            ptree = read_pt_len(bits, 14, 4, -1)
        remaining -= 1
        c = ctree.decode(bits)
        if c < 256:
            out.append(c)
            continue
        length = c - 256 + 3
        p = ptree.decode(bits)
        if p != 0:
            p = (1 << (p - 1)) + bits.get(p - 1)
        start = len(out) - p - 1
        for k in range(length):
            out.append(out[start + k])
    return bytes(out[:size])


def members(data):
    pos = data.find(b'-lh')
    while pos >= 0:
        start = pos - 2
        hsize = data[start]
        method = data[pos:pos + 5].decode('latin-1')
        packed, original = struct.unpack_from('<II', data, pos + 5)
        level = data[start + 20]
        namelen = data[start + 21]
        name = data[start + 22:start + 22 + namelen].decode('latin-1')
        body = start + 2 + hsize
        if level == 1:
            ext = struct.unpack_from('<H', data, start + 2 + hsize - 2)[0]
            while ext:
                ext_next = struct.unpack_from('<H', data, body + ext - 2)[0]
                body += ext
                packed -= ext
                ext = ext_next
        yield name, method, packed, original, body
        pos = data.find(b'-lh', body + packed)


def main():
    data = open(sys.argv[1], 'rb').read()
    if len(sys.argv) == 2:
        for name, method, packed, original, body in members(data):
            print(method, packed, original, name)
        return
    want = sys.argv[2].lower().replace('/', '\\')
    for name, method, packed, original, body in members(data):
        if name.lower().replace('/', '\\').endswith(want):
            raw = data[body:body + packed]
            content = raw if method == '-lh0-' else lh5(raw, original)
            open(sys.argv[3], 'wb').write(content)
            print('wrote', sys.argv[3], len(content))
            return
    print('not found')


main()
