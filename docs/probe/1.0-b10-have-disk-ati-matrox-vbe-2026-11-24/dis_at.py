"""Disassemble DESK.CPL from given VAs; print strings at given VAs; name IAT slots."""
import sys

import capstone
import pefile

pe = pefile.PE(sys.argv[1])
data = pe.get_memory_mapped_image()
base = pe.OPTIONAL_HEADER.ImageBase
iat = {}
for entry in pe.DIRECTORY_ENTRY_IMPORT:
    for imp in entry.imports:
        iat[imp.address] = "%s!%s" % (entry.dll.decode(), (imp.name or b"#%d" % imp.ordinal).decode())

for arg in sys.argv[2:]:
    kind, va, n = arg.split(":")
    va = int(va, 16)
    n = int(n, 0)
    if kind == "s":
        raw = data[va - base:va - base + n]
        print("str %08x: %r" % (va, raw.split(b"\x00")[0]))
        continue
    print("\n=== code %08x" % va)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for ins in md.disasm(data[va - base:va - base + n], va):
        note = ""
        for slot, name in iat.items():
            if ("0x%x]" % slot) in ins.op_str:
                note = "   ; " + name
        print("%08x  %-8s %s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
