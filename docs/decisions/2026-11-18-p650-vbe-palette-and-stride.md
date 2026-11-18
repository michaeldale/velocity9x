# Matrox Millennium P650 on the vbe driver: palette through 4F09h, no stride set

Date: 2026-11-18. Machine: M-BXMFA8 (W9QI3632), Daniel's, where the
GeForce 210 was until 2026-11-17. Card: Matrox Millennium P650, PCI
102B:2538, VBE 3.0, BIOS "MATROX/P650/P750 VGA/VBE BIOS (V1.3) b11".
Status: fix built (run-checks green), running on the card, and the screen
confirmed by the owner (below). The owner had switched the adapter back to
Microsoft's "Standard PCI Graphics Adapter (VGA)", so the package was
reinstalled remotely by Have Disk from C:\V9XP650 (Display\0001) and the
saved mode set to 640x480x8. Boot 170: `Stage=enable-ok`,
`Palette=route=1 vbe=1` (4F09h answered 004Fh), `DrawPitch=640`,
`VbeScanBytes=1024`, `VbeScanBefore=0` (no set). Records in
`docs/probe/p650-bxmfa8-vbe-2026-11-18/boot170-*`.

## What was reported

Daniel installed the 0.5x vbe package and photographed 640x480x8: the
desktop cut into horizontal blocks, each block's text legible at normal
size but displaced sideways; black bands; title bars red, the desktop
orange-brown.

## Evidence

Records in `docs/probe/p650-bxmfa8-vbe-2026-11-18/` (V9XBOOT, V9XHW,
V9XMODES, V9XTIME). The VBIOS image was read with `V9XTRACE -vbios` and
disassembled; it is Matrox's code and is not in the tree.

- `VbeController=v=0300 mem=256 caps=00000003`: capabilities bit 1,
  "controller is not VGA compatible". No other card in `docs/probe/` reports
  that bit. Mode attributes `00BBh` (bit 5, not a VGA mode) are on 1,726
  records from cards whose palettes load fine through the ports, so the
  attribute bit is no discriminator.
- `VbeMode00=m=0101 ... s=640/640`, while every other mode up to 1024 wide
  has a 1024-pixel stride. `VbeScanBefore=1024`, `VbeScanBytes=640`: after
  the mode set 4F06h's get said 1024, the driver set 640, and the card then
  said 640.
- V9XTIME `/mga`: CR13 = 28h (640 bytes at 8 bpp), standard 640x480
  timings; the Matrox CRTCEXT registers at 3DEh read FFh (not decoded on this
  card).

From the VBIOS (offsets in the 46 KiB image):

- **Palette.** INT 10h AX=1012h (set DAC block, `1E70h`) checks the BIOS
  mode byte at 0:449h and, in the extended modes, writes the native LUT
  registers (red 1A3Ch, green 1A40h, blue 1A44h, through `7834h`) instead
  of ports 3C8h/3C9h. 4F09h (`6384h`) uses the same routine. AX=1010h,
  the single-register set, writes the ports in every mode. So the ports
  this driver programs reach a table the extended modes do not scan, and
  the screen kept the BIOS's default VGA palette: Windows' navy (index 4)
  shows as VGA red, teal (6) as VGA brown, white (255) as VGA black. That
  matches the photograph's colours entry for entry.
- **Stride.** 4F02h (`5E57h`) calls `A55Ch` after setting mode 101h when
  ROM flag byte 7FECh bit 0 is set (it is: 03h), `A5F3h` for any other mode.
  `A55Ch` programs a group of registers (1220h-122Ch, 1A24h, 1324h, 404h,
  408h, 1DFCh) and leaves the pitch register (index 8) at 1024. 4F01h
  reports 640 for 101h under the same flag (`6200h`). 4F06h's get reads
  register 8 (1024); its set, for 101h, writes 640 into register 8 whatever
  is asked for, and leaves the `A55Ch` registers alone. Reading: the BIOS
  presents 101h at a 640-byte stride through a remapping over hardware that
  scans at 1024, and the driver's corrective set rewrote the hardware pitch
  underneath that remapping. The block-wise displacement with legible
  contents is what a remapped aperture scanned at the wrong pitch would
  give. What the `A55Ch` registers do is inferred from that, not from a
  datasheet; none is public for this chip.

Hypotheses killed: a pitch mismatch at the line level (would compress or
shear the glyphs; they are intact), and a 6/8-bit DAC width mismatch
(would darken, not swap, the colours).

## Change

- Mini-VDD API 0031h VBE_PAL_STAGE and 0032h VBE_PAL_LOAD
  (`include/asm/V9XMAPI.INC`): four entries per call into the init-time
  512-byte V86 scratch, then 4F09h BL=00h by nested V86 execution. No
  caller pointer crosses into the VxD.
- `src/display16/palette16.c` (new; ddi.c's palette load moved there): when
  4F00h says the controller is not VGA compatible, the palette goes through
  those calls; any failure drops back to the ports.
  `Palette=route=<0 ports|1 vbe> vbe=<last load result>` in V9XBOOT.INI.
  The ati family compiles it into LEAFCODE and runtime.asm's two wrappers
  sit in SSWCODE: the ati driver's `_TEXT` had about 30 bytes of its
  57,344-byte budget left, and the first build of this change was 451 over.
- `enable16.c` stage 9: on such a controller the stride 4F01h describes is
  kept and 4F06h is not set (trace `pitch-bios-view`).

Both changes key on capabilities bit 1, which only the P650 reports among
the recorded cards, so no other card's path changes.

## Owner's check

Daniel's eyes are the only check, since every readback the driver can make
goes through the same remapped aperture. His reports on the remote-help
chat, 2026-11-18 (UTC): at 14:32, "no corruption now in 640x480 256
colours"; at 14:44, 640x480, 800x600, 1024x768 and 1280x1024 look fine at
8, 16 and 32 bpp. No photo was sent; the record is his description. Only
640x480x8 was reported broken. The palette route matters only at 8 bpp;
the kept stride applies to every mode on this card, so the other modes
show that skipping the 4F06h set did no harm there.

## Not known yet

- Whether a stride of 1024 drawn at 640x480x8 would also work; not tried.
- The palette load in `ResetHiResMode` (return from a full-screen DOS box)
  now runs a nested BIOS call from inside that VDD callback on this card.
  The mode set there already goes through INT 10h; untested.
- DirectDraw palettes (8 bpp games) go through the same `v9x_program_palette`;
  untested on the card.
