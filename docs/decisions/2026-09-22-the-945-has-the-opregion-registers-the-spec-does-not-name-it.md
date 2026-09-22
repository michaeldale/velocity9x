# The 945 has the OpRegion registers; the specification does not name it

2026-09-22, desk work, no hardware. Reviewing whether the Intel IGD
OpRegion Specification rev 1.0 (October 2008), found in the Wayback
inventory and vendored at `docs\reference\`, describes anything the
945GSE in MICHAEL-NETBOOK actually implements.

Short answer: the silicon interface is in the 945's own datasheet, so the
mechanism exists on this part. The document does not say so, one of its
three mailboxes is a Gen4 replacement for the Gen3 mechanism, and whether
this netbook's BIOS allocates an OpRegion at all is untested.

## Against the document

The specification's version field enumerates its parts: "1.0 - Broadwater
(supports Mailbox #2), 1.1 - Crestline (supports Mailbox #1, #2, 3), 2.0
- Cantiga". That is 965G, GM965 and GM45. The strings "945" and "915" do
not occur anywhere in its 133 pages. Taken on its own the document says
nothing about Gen3.

## For the silicon

Intel's datasheet for this part says otherwise, in the Device 2
configuration space (309219-006, chapter 8):

| Register | Offset | What |
|---|---|---|
| `ASLS` - ASL Storage | D2:F0 `FC-FFh`, R/W, 32 bits | the OpRegion pointer. The datasheet describes its purpose as "storage for switching/indicating up to 6 devices", two bits for `_DOD`, one for `_DGS`, two for `_DCS` - ACPI display switching, which is Mailbox #1's subject |
| `ASLE` - System Display Event | D2:F0 `E4-E7h`, R/W, 32 bits | four scratch trigger bytes, each raising an interrupt when IEF bit 0 is enabled and IMR bit 0 unmasked - the doorbell Mailbox #3 rings |
| `SWSMI` - Software SMI | D2:F0 `E0-E1h`, 16 bits | bit 0 triggers an SMI; the upper byte is the caller and function |
| `LBB` - Legacy Backlight Brightness | D2:F1 `F4-F7h`, R/W, 32 bits | the pre-OpRegion backlight path. Byte 7:0 is a brightness request, 0 darkest and 255 brightest; writing it sets LBES in PIPEBSTATUS and raises a Display B interrupt if LBEE is enabled. It signals the driver, it does not drive the panel |

`SWSMI`, `ASLE` and `ASLS` are in the desktop 945 datasheet too
(307502-005, 7.1.30 to 7.1.32), and `SWSMI`, `LBB` and `ASLS` in the
Mobile 915/910 datasheet (305264-002, 7.2.32, 7.2.34, 7.2.35). This is
Gen3-wide, not a mobile-945 particularity.

Linux agrees at the discovery level. Upstream `intel_opregion_setup`
reads `ASLS` from PCI config `0xfc` with **no generation test**, maps 8
KB there, checks the `IntelGraphicsMem` signature and reads the header's
mailbox bitmask; what it gates on afterwards is the OpRegion's own
version, not the silicon's - the extended VBT path requires major >= 2.
A driver on Gen3 is expected to ask the header, not the part.

## Where the document stops applying

Mailbox #2 is SWSCI, and the specification introduces the whole scheme as
"the SMI replacement". The 945 has `SWSMI`, an SMI, at `E0h`. So Mailbox
#2's contents describe a path this silicon does not have, whatever a 945
BIOS puts in the header.

One layout discrepancy, recorded so it is not walked into: the
specification's Table 2-1 places Mailbox #3 at `0x0300-0x0499` and the
VBT at `0x0500-0x1C99`, while i915 uses `OPREGION_VBT_OFFSET 0x400`.
Anything that reads a VBT out of an OpRegion should follow the header and
the ASLE sizes, not either constant.

## What it is worth here

Nothing for 3D. Possibly something for the panel: backlight, lid and
display switching on this class of machine go through ASLE in modern
drivers, and the netbook is the only panel-driven target in the fleet.
Note what the datasheet's own backlight register implies - `LBB` and
`ASLE` are both doorbells INTO a driver, so the OpRegion is a protocol
for a driver that owns the panel. Velocity9x sets modes through the VBE
BIOS and owns nothing of the sort, which is why none of this is on a
current path.

## The probe that would settle it, read-only

On MICHAEL-NETBOOK, read D2:F0 configuration offset `FCh`. Zero means the
BIOS allocated no OpRegion and the question ends. Non-zero is a physical
address: map 8 KB there and record the header - `SIGN` at `0000h` (16
bytes, expect `IntelGraphicsMem`), `SIZE` at `0010h`, `OVER` at `0014h`,
`MBOX` at `0058h` (the bitmask of mailboxes the BIOS claims to support)
and `DMOD` at `005Ch`. Four dwords and a string answer the applicability
question outright. No writes; the write side of any mailbox is a driver
handshake with the system BIOS and is not something to try blind.

Until that runs, the honest statement is: the registers exist on this
silicon by Intel's own datasheet, and nobody has looked at this machine.
