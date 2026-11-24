"""Find DESK.CPL code that references the "Tag" string and disassemble around it."""
import sys

import capstone
import pefile

path = sys.argv[1]
pe = pefile.PE(path)
data = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase

def find_all(needle):
    out = []
    start = 0
    while True:
        i = data.find(needle, start)
        if i < 0:
            return out
        out.append(i)
        start = i + 1

targets = {}
for name in (b"Tag\x00", b"PropertySheetHandlers", b"Controls Folder"):
    for off in find_all(name):
        targets[base + off] = name
for va, name in sorted(targets.items()):
    print("string %r at %08x" % (name, va))

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
text = [s for s in pe.sections if s.Name.startswith(b".text")][0]
tstart = text.VirtualAddress
tcode = data[tstart:tstart + text.Misc_VirtualSize]
refs = []
for va in targets:
    le = va.to_bytes(4, "little")
    i = 0
    while True:
        j = tcode.find(le, i)
        if j < 0:
            break
        refs.append((base + tstart + j, va))
        i = j + 1
for ref, va in sorted(refs):
    print("\n=== ref to %08x (%r) at %08x" % (va, targets[va], ref))
    lo = ref - 0x120
    hi = ref + 0x160
    chunk = data[lo - base:hi - base]
    for ins in md.disasm(chunk, lo):
        if ins.address < ref - 0x100:
            continue
        print("%08x  %-8s %s" % (ins.address, ins.mnemonic, ins.op_str))
