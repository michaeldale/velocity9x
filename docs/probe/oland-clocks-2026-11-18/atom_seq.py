"""Turn atom_sim.py's trace of a clock table into a V9XSIMM /seq file.

Every register write becomes "W offset value", in order. A read of a PLL
status register immediately followed by a TEST is one of the table's polls
and becomes "P offset mask": SPLL_STATUS (614h) waits for bit 1,
MPLL_CNTL_MODE (2BB0h) for MPLL_CHG_STATUS, bit 16. A DELAY becomes
"D 0 microseconds". Other reads only fed read-modify-writes or branches,
whose outcome the simulated run already took with the card's values.
Refuses any write outside the allow-list the VxD enforces too.

usage: atom_seq.py TRACE.txt OUT.seq
"""
import re
import sys

ALLOWED = {0x600, 0x604, 0x608, 0x610, 0x618, 0x620,
           0x2774, 0x2778, 0x27B0, 0x2808,
           0x20AC, 0x5490, 0x27AC,
           0x2888, 0x2898, 0x28A0, 0x28A4, 0x28A8, 0x28AC, 0x28B0, 0x28BC,
           0x28C0, 0x28C4, 0x292C, 0x2944, 0x295C, 0x297C, 0x2A04, 0x2A08,
           0x2A0C, 0x2A68, 0x2AAC, 0x2B44, 0x2B54,
           0x2BA0, 0x2BA4, 0x2BB0, 0x2BB4, 0x2BB8, 0x2BC0, 0x2BC4, 0x2BC8,
           0x2BD4}
POLLS = {0x614: 0x2, 0x2BB0: 0x10000}

lines = open(sys.argv[1], encoding='latin-1').read().splitlines()
ops = []
for i, line in enumerate(lines):
    w = re.match(r'^\s*W ([0-9A-F]{5}) <- ([0-9A-F]{8})', line)
    r = re.match(r'^\s*R ([0-9A-F]{5}) ', line)
    d = re.match(r'^\s*DELAY (\d+) (us|ms)', line)
    if w:
        off = int(w.group(1), 16)
        if off not in ALLOWED:
            sys.exit('write to %05X is outside the allow-list' % off)
        ops.append('W %05X %s' % (off, w.group(2)))
    elif r:
        off = int(r.group(1), 16)
        polled = i + 1 < len(lines) and 'TEST' in lines[i + 1]
        if off in POLLS and (off == 0x614 or polled):
            # Consecutive polls of one register collapse into one.
            op = 'P %05X %08X' % (off, POLLS[off])
            if not ops or ops[-1] != op:
                ops.append(op)
    elif d:
        us = int(d.group(1)) * (1000 if d.group(2) == 'ms' else 1)
        ops.append('D 00000 %08X' % us)
    elif re.search(r' (PLL|MC) [RW] ', line):
        sys.exit('PLL/MC access in trace: %s' % line.strip())
with open(sys.argv[2], 'w', newline='') as out:
    out.write('\r\n'.join(ops) + '\r\n')
print('%d ops' % len(ops))
