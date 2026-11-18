"""A small AtomBIOS command-table interpreter for analysis, not for hardware.

Runs a VBIOS command table against a simulated register file seeded from a
V9XSIMM.INI snapshot and logs every register, PLL and MC access. Follows
the operand and opcode semantics of Linux drivers/gpu/drm/radeon/atom.c as
recalled (no copy at hand); the self-check is that SetEngineClock at the
current clock reproduces the SPLL registers the card already holds.

usage: atom_sim.py ROM INI TABLE PARAM0 [PARAM1 ...]
"""
import re
import struct
import sys

ARG_REG, ARG_PS, ARG_WS, ARG_FB, ARG_ID, ARG_IMM, ARG_PLL, ARG_MC = range(8)
SRC_DWORD, SRC_WORD0, SRC_WORD8, SRC_WORD16 = 0, 1, 2, 3
ARG_MASK = [0xFFFFFFFF, 0xFFFF, 0xFFFF00, 0xFFFF0000, 0xFF, 0xFF00, 0xFF0000,
            0xFF000000]
ARG_SHIFT = [0, 0, 8, 16, 0, 8, 16, 24]
DST_TO_SRC = [[0, 0, 0, 0], [1, 2, 3, 0], [1, 2, 3, 0], [1, 2, 3, 0],
              [4, 5, 6, 7], [4, 5, 6, 7], [4, 5, 6, 7], [4, 5, 6, 7]]
DEF_DST = [0, 0, 1, 2, 0, 1, 2, 3]

WS_QUOTIENT, WS_REMAINDER, WS_DATAPTR, WS_SHIFT = 0x40, 0x41, 0x42, 0x43
WS_OR_MASK, WS_AND_MASK, WS_FB_WINDOW, WS_ATTRIBUTES, WS_REGPTR = (
    0x44, 0x45, 0x46, 0x47, 0x48)

TABLE_NAMES = {0: 'ASIC_Init', 5: 'MemoryControllerInit', 10: 'SetEngineClock',
               11: 'SetMemoryClock', 14: 'ResetMemoryDLL',
               15: 'ResetMemoryDevice', 16: 'MemoryPLLInit',
               18: 'AdjustMemoryController', 48: 'GetEngineClock',
               47: 'GetMemoryClock', 59: 'MC_Synchronization',
               60: 'ComputeMemoryEnginePLL', 64: 'MemoryTraining',
               67: 'SetVoltage', 70: 'ComputeMemoryClockParam',
               80: 'GetVoltageInfo'}


class Halt(Exception):
    pass


class Atom:
    def __init__(self, rom, regs):
        self.rom = rom
        self.regs = dict(regs)
        self.log = []
        hdr = self.u16(0x48)
        self.cmd_table = self.u16(hdr + 0x1E)
        self.data_table = self.u16(hdr + 0x20)
        self.unknown_reads = set()
        self.steps = 0
        self.divmul = [0, 0]
        self.fb_base = 0
        self.io_attr = 0
        self.reg_block = 0
        self.shift = 0
        self.data_block = 0
        self.scratch = {}
        self.depth = 0

    def u8(self, o):
        return self.rom[o]

    def u16(self, o):
        return struct.unpack_from('<H', self.rom, o)[0]

    def u32(self, o):
        return struct.unpack_from('<I', self.rom, o)[0]

    # --- the simulated card -------------------------------------------------
    def reg_read(self, idx):
        off = idx * 4
        if off not in self.regs:
            self.unknown_reads.add(off)
        val = self.regs.get(off, 0)
        self.log.append('%s  R %05X -> %08X' % ('  ' * self.depth, off, val))
        return val

    def reg_write(self, idx, val):
        off = idx * 4
        self.log.append('%s  W %05X <- %08X' % ('  ' * self.depth, off, val))
        self.regs[off] = val & 0xFFFFFFFF
        self.model(off)

    def model(self, off):
        """Hardware behaviour the tables poll for: report a requested
        SPLL change as done at once, so a poll loop ends."""
        if off == 0x604:
            self.regs[0x614] = self.regs.get(0x614, 0) | 0x2
        # MPLL_CNTL_MODE: MPLL_CTLREQ (bit 14) acknowledged by
        # MPLL_CHG_STATUS (bit 16), gmc_6_0_sh_mask.h.
        if off == 0x2BB0 and (self.regs[off] & (1 << 14)):
            self.regs[off] |= 1 << 16

    def pll_read(self, idx):
        self.log.append('%s  PLL R %02X' % ('  ' * self.depth, idx))
        return 0

    def pll_write(self, idx, val):
        self.log.append('%s  PLL W %02X <- %08X' % ('  ' * self.depth, idx, val))

    def mc_read(self, idx):
        self.log.append('%s  MC R %02X' % ('  ' * self.depth, idx))
        return 0

    def mc_write(self, idx, val):
        self.log.append('%s  MC W %02X <- %08X' % ('  ' * self.depth, idx, val))

    # --- operands -------------------------------------------------------------
    def get_src_int(self, ctx, attr, p, want_saved=False):
        arg = attr & 7
        align = (attr >> 3) & 7
        if arg == ARG_REG:
            idx = self.u16(p[0]) + self.reg_block
            p[0] += 2
            if ctx['io_mode'] != 0:
                self.log.append('%s  (io_mode %X)' % ('  ' * self.depth, ctx['io_mode']))
            val = self.reg_read(idx)
        elif arg == ARG_PS:
            idx = self.u8(p[0])
            p[0] += 1
            val = ctx['ps'][ctx['ps_shift'] + idx]
        elif arg == ARG_WS:
            idx = self.u8(p[0])
            p[0] += 1
            val = {WS_QUOTIENT: self.divmul[0], WS_REMAINDER: self.divmul[1],
                   WS_DATAPTR: self.data_block, WS_SHIFT: self.shift,
                   WS_OR_MASK: (1 << self.shift) & 0xFFFFFFFF,
                   WS_AND_MASK: (~(1 << self.shift)) & 0xFFFFFFFF,
                   WS_FB_WINDOW: self.fb_base, WS_ATTRIBUTES: self.io_attr,
                   WS_REGPTR: self.reg_block}.get(idx)
            if val is None:
                val = ctx['ws'][idx]
        elif arg == ARG_ID:
            idx = self.u16(p[0])
            p[0] += 2
            val = self.u32(idx + self.data_block)
        elif arg == ARG_FB:
            idx = self.u8(p[0])
            p[0] += 1
            val = self.scratch.get(self.fb_base // 4 + idx, 0)
        elif arg == ARG_IMM:
            if align == SRC_DWORD:
                val = self.u32(p[0])
                p[0] += 4
            elif align in (1, 2, 3):
                val = self.u16(p[0])
                p[0] += 2
            else:
                val = self.u8(p[0])
                p[0] += 1
            return (val, val) if want_saved else val
        elif arg == ARG_PLL:
            idx = self.u8(p[0])
            p[0] += 1
            val = self.pll_read(idx)
        else:
            idx = self.u8(p[0])
            p[0] += 1
            val = self.mc_read(idx)
        saved = val
        val = (val & ARG_MASK[align]) >> ARG_SHIFT[align]
        return (val, saved) if want_saved else val

    def get_src(self, ctx, attr, p):
        return self.get_src_int(ctx, attr, p)

    def get_dst(self, ctx, arg, attr, p):
        return self.get_src_int(
            ctx, arg | (DST_TO_SRC[(attr >> 3) & 7][(attr >> 6) & 3] << 3), p,
            want_saved=True)

    def skip_dst(self, ctx, arg, attr, p):
        size = {ARG_REG: 2, ARG_ID: 2}.get(arg, 1)
        p[0] += size

    def get_src_direct(self, align, p):
        if align == SRC_DWORD:
            val = self.u32(p[0]); p[0] += 4
        elif align in (1, 2, 3):
            val = self.u16(p[0]); p[0] += 2
        else:
            val = self.u8(p[0]); p[0] += 1
        return val

    def put_dst(self, ctx, arg, attr, p, val, saved):
        align = DST_TO_SRC[(attr >> 3) & 7][(attr >> 6) & 3]
        val = ((val << ARG_SHIFT[align]) & ARG_MASK[align]) & 0xFFFFFFFF
        saved &= ~ARG_MASK[align] & 0xFFFFFFFF
        val |= saved
        if arg == ARG_REG:
            idx = self.u16(p[0]) + self.reg_block
            p[0] += 2
            self.reg_write(idx, val)
        elif arg == ARG_PS:
            idx = self.u8(p[0]); p[0] += 1
            ctx['ps'][ctx['ps_shift'] + idx] = val
        elif arg == ARG_WS:
            idx = self.u8(p[0]); p[0] += 1
            if idx == WS_QUOTIENT:
                self.divmul[0] = val
            elif idx == WS_REMAINDER:
                self.divmul[1] = val
            elif idx == WS_DATAPTR:
                self.data_block = val
            elif idx == WS_SHIFT:
                self.shift = val
            elif idx in (WS_OR_MASK, WS_AND_MASK):
                pass
            elif idx == WS_FB_WINDOW:
                self.fb_base = val
            elif idx == WS_ATTRIBUTES:
                self.io_attr = val
            elif idx == WS_REGPTR:
                self.reg_block = val
            else:
                ctx['ws'][idx] = val
        elif arg == ARG_FB:
            idx = self.u8(p[0]); p[0] += 1
            self.scratch[self.fb_base // 4 + idx] = val
        elif arg == ARG_PLL:
            idx = self.u8(p[0]); p[0] += 1
            self.pll_write(idx, val)
        elif arg == ARG_MC:
            idx = self.u8(p[0]); p[0] += 1
            self.mc_write(idx, val)
        else:
            raise Halt('put to arg %d' % arg)

    # --- opcodes ----------------------------------------------------------------
    def binop(self, ctx, p, arg, fn):
        attr = self.u8(p[0]); p[0] += 1
        dptr = [p[0]]
        dst, saved = self.get_dst(ctx, arg, attr, p)
        src = self.get_src(ctx, attr, p)
        self.put_dst(ctx, arg, attr, dptr, fn(dst, src) & 0xFFFFFFFF, saved)

    def run(self, index, ps, ps_shift=0):
        base = self.u16(self.cmd_table + 4 + 2 * index)
        if base == 0:
            raise Halt('table %d absent' % index)
        size = self.u16(base)
        ws_size = self.u8(base + 4)
        ps_size = self.u8(base + 5) & 0x7F
        self.log.append('%s CALL %d %s (base %04X size %d ws %d ps %d)' % (
            '  ' * self.depth, index, TABLE_NAMES.get(index, ''), base, size,
            ws_size, ps_size))
        ctx = {'ps': ps, 'ps_shift': ps_shift, 'ws': [0] * 256, 'start': base,
               'io_mode': 0, 'cs_equal': False, 'cs_above': False,
               'ps_size': ps_size}
        p = [base + 6]
        self.depth += 1
        try:
            while True:
                self.steps += 1
                if self.steps > 200000:
                    raise Halt('step limit (a poll loop that never ends?)')
                op = self.u8(p[0]); p[0] += 1
                if not self.step(ctx, op, p):
                    break
        finally:
            self.depth -= 1

    def step(self, ctx, op, p):
        if 1 <= op <= 6:
            arg = op - 1
            attr = self.u8(p[0]); p[0] += 1
            dptr = [p[0]]
            if ((attr >> 3) & 7) != SRC_DWORD:
                _, saved = self.get_dst(ctx, arg, attr, p)
            else:
                self.skip_dst(ctx, arg, attr, p)
                saved = 0xCDCDCDCD
            src = self.get_src(ctx, attr, p)
            self.put_dst(ctx, arg, attr, dptr, src, saved)
        elif 7 <= op <= 12:
            self.binop(ctx, p, op - 7, lambda d, s: d & s)
        elif 13 <= op <= 18:
            self.binop(ctx, p, op - 13, lambda d, s: d | s)
        elif 19 <= op <= 30:
            arg = (op - 19) % 6
            left = op < 25
            attr = self.u8(p[0]); p[0] += 1
            dptr = [p[0]]
            attr &= 0x38
            attr |= DEF_DST[attr >> 3] << 6
            dst, saved = self.get_dst(ctx, arg, attr, p)
            shift = self.u8(p[0]); p[0] += 1
            dst = (dst << shift) if left else (dst >> shift)
            self.put_dst(ctx, arg, attr, dptr, dst & 0xFFFFFFFF, saved)
        elif 31 <= op <= 36:
            arg = op - 31
            attr = self.u8(p[0]); p[0] += 1
            dst, _ = self.get_dst(ctx, arg, attr, p)
            src = self.get_src(ctx, attr, p)
            self.divmul[0] = (dst * src) & 0xFFFFFFFF
        elif 37 <= op <= 42:
            arg = op - 37
            attr = self.u8(p[0]); p[0] += 1
            dst, _ = self.get_dst(ctx, arg, attr, p)
            src = self.get_src(ctx, attr, p)
            if src:
                self.divmul = [dst // src, dst % src]
            else:
                self.divmul = [0, 0]
        elif 43 <= op <= 48:
            self.binop(ctx, p, op - 43, lambda d, s: d + s)
        elif 49 <= op <= 54:
            self.binop(ctx, p, op - 49, lambda d, s: d - s)
        elif op == 55:
            port = self.u16(p[0]); p[0] += 2
            ctx['io_mode'] = 0 if port == 0 else (0x80 | port)
        elif op in (56, 57):
            p[0] += 1
            ctx['io_mode'] = 0x100 if op == 56 else 0x200
        elif op == 58:
            self.reg_block = self.u16(p[0]); p[0] += 2
        elif op == 59:
            attr = self.u8(p[0]); p[0] += 1
            self.fb_base = self.get_src(ctx, attr, p)
        elif 60 <= op <= 65:
            arg = op - 60
            attr = self.u8(p[0]); p[0] += 1
            dst, _ = self.get_dst(ctx, arg, attr, p)
            src = self.get_src(ctx, attr, p)
            ctx['cs_equal'] = dst == src
            ctx['cs_above'] = dst > src
            self.log.append('%s  CMP %08X vs %08X' % ('  ' * self.depth, dst, src))
        elif op == 66:
            attr = self.u8(p[0]); p[0] += 1
            src = self.get_src(ctx, attr, p)
            while self.u16(p[0]) != 0x5A5A:
                if self.u8(p[0]) != 0x63:
                    raise Halt('bad switch case at %04X' % p[0])
                p[0] += 1
                val = self.get_src(ctx, (attr & 0x38) | ARG_IMM, p)
                target = self.u16(p[0])
                if val == src:
                    p[0] = ctx['start'] + target
                    return True
                p[0] += 2
            p[0] += 2
        elif 67 <= op <= 73:
            target = self.u16(p[0]); p[0] += 2
            eq, ab = ctx['cs_equal'], ctx['cs_above']
            take = {67: True, 68: eq, 69: (not eq) and (not ab), 70: ab,
                    71: not ab, 72: ab or eq, 73: not eq}[op]
            if take:
                p[0] = ctx['start'] + target
        elif 74 <= op <= 79:
            arg = op - 74
            attr = self.u8(p[0]); p[0] += 1
            dst, _ = self.get_dst(ctx, arg, attr, p)
            src = self.get_src(ctx, attr, p)
            ctx['cs_equal'] = (dst & src) == 0
            self.log.append('%s  TEST %08X & %08X' % ('  ' * self.depth, dst, src))
        elif op in (80, 81):
            count = self.u8(p[0]); p[0] += 1
            self.log.append('%s  DELAY %d %s' % ('  ' * self.depth, count,
                                                 'ms' if op == 80 else 'us'))
        elif op == 82:
            idx = self.u8(p[0]); p[0] += 1
            self.run(idx, ctx['ps'], ctx['ps_shift'] + ctx['ps_size'] // 4)
        elif op == 83:
            raise Halt('REPEAT')
        elif 84 <= op <= 89:
            arg = op - 84
            attr = self.u8(p[0]); p[0] += 1
            dptr = [p[0]]
            attr &= 0x38
            attr |= DEF_DST[attr >> 3] << 6
            _, saved = self.get_dst(ctx, arg, attr, p)
            self.put_dst(ctx, arg, attr, dptr, 0, saved)
        elif op == 90:
            pass
        elif op == 91:
            return False
        elif 92 <= op <= 97:
            arg = op - 92
            attr = self.u8(p[0]); p[0] += 1
            dptr = [p[0]]
            dst, saved = self.get_dst(ctx, arg, attr, p)
            mask = self.get_src_direct((attr >> 3) & 7, p)
            src = self.get_src(ctx, attr, p)
            self.put_dst(ctx, arg, attr, dptr, (dst & mask) | src, saved)
        elif op == 98:
            p[0] += 1
        elif op == 99:
            pass
        elif op in (100, 101):
            raise Halt('SAVEREG/RESTOREREG')
        elif op == 102:
            idx = self.u8(p[0]); p[0] += 1
            if idx == 0:
                self.data_block = 0
            elif idx == 255:
                self.data_block = ctx['start']
            else:
                self.data_block = self.u16(self.data_table + 4 + 2 * idx)
        elif 103 <= op <= 108:
            self.binop(ctx, p, op - 103, lambda d, s: d ^ s)
        elif 109 <= op <= 120:
            arg = (op - 109) % 6
            left = op < 115
            attr = self.u8(p[0]); p[0] += 1
            dptr = [p[0]]
            dst_align = DST_TO_SRC[(attr >> 3) & 7][(attr >> 6) & 3]
            _, saved = self.get_dst(ctx, arg, attr, p)
            dst = saved
            shift = self.get_src(ctx, attr, p)
            dst = (dst << shift) if left else (dst >> shift)
            dst = ((dst & ARG_MASK[dst_align]) >> ARG_SHIFT[dst_align])
            self.put_dst(ctx, arg, attr, dptr, dst & 0xFFFFFFFF, saved)
        elif op == 121:
            p[0] += 1
        elif op == 122:
            length = self.u16(p[0]); p[0] += 4 + length
        elif op in (123, 124):
            arg = ARG_PS if op == 123 else ARG_WS
            attr = self.u8(p[0]); p[0] += 1
            dst, _ = self.get_dst(ctx, arg, attr, p)
            src = self.get_src(ctx, attr, p)
            v = dst * src
            self.divmul = [v & 0xFFFFFFFF, (v >> 32) & 0xFFFFFFFF]
        elif op in (125, 126):
            arg = ARG_PS if op == 125 else ARG_WS
            attr = self.u8(p[0]); p[0] += 1
            dst, _ = self.get_dst(ctx, arg, attr, p)
            src = self.get_src(ctx, attr, p)
            if src:
                v = (self.divmul[1] << 32) | dst
                self.divmul = [(v // src) & 0xFFFFFFFF, (v % src) & 0xFFFFFFFF]
            else:
                self.divmul = [0, 0]
        else:
            raise Halt('unknown opcode %d at %04X' % (op, p[0] - 1))
        return True


def load_regs(ini):
    regs = {}
    for line in open(ini, encoding='latin-1'):
        m = re.match(r'^R([0-9A-F]{5})=([0-9A-F]{8})', line.strip())
        if m:
            regs[int(m.group(1), 16)] = int(m.group(2), 16)
    return regs


def main():
    rom = open(sys.argv[1], 'rb').read()
    regs = load_regs(sys.argv[2])
    table = int(sys.argv[3])
    params = [int(x, 0) for x in sys.argv[4:]] + [0] * 64
    atom = Atom(rom, regs)
    try:
        atom.run(table, params)
        status = 'EOT'
    except Halt as e:
        status = 'HALT: %s' % e
    for line in atom.log:
        print(line)
    print('status', status, 'steps', atom.steps)
    print('ps', ' '.join('%08X' % v for v in params[:8]))
    print('registers not in the snapshot that were read:',
          ' '.join('%05X' % o for o in sorted(atom.unknown_reads)))
    changed = {o: v for o, v in atom.regs.items() if regs.get(o) != v}
    for o in sorted(changed):
        print('final %05X %08X (was %s)' % (o, changed[o],
              '%08X' % regs[o] if o in regs else 'unread'))


main()
