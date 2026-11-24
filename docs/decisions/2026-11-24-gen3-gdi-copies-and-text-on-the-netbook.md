# Gen3 GDI on the netbook: copies on the blitter 13 to 47 times faster, text on the blitter 3 to 5 times slower

Date: 2026-11-24. A build of the author's Gen3-only branch with the GDI
copies, the hardware cursor and a GDI text path on the blitter, and
V9XGDIB.EXE (a copy and text timer, not part of this change) from the same
commit. Machine: the GMA 950 netbook (945GSE, Atom), Windows 98 SE,
800x600 at 32 bpp. Plans: `docs/plans/gen3-gdi-blitter.md`,
`docs/plans/gen3-hardware-cursor.md`.

## Evidence

Three V9XGDIB.EXE reports, copied from the user's stick unchanged except
for line endings:

- [GPU-ALL](2026-11-24-gen3-gdi-netbook-gpu-all.txt): copies and text on
  (`enabled=26`, copy, overlap and text).
- [GPU-NOTEXT](2026-11-24-gen3-gdi-netbook-gpu-notext.txt): meant to be
  text off with copies on, but it reads `enabled=0` and counts every
  BitBlt as `disabled`. `GdiAccelText=0` alone leaves copies on
  (`v9x_gdi_accel_configure`), so this run had everything off as well; the
  SYSTEM.INI it ran with was not sent. It is a second "off" run.
- [GPU-OFF](2026-11-24-gen3-gdi-netbook-gpu-off.txt): `GdiAccel=0`.

All three: `result: PASS, 0 check(s) failed` - the eight copies and the
text in System, Arial 16 and Courier New 20 bold match the DIB Engine's
own drawing pixel for pixel. So the XY_MONO_SRC_COPY_IMMEDIATE bit order,
row padding and transparency bit taken from SNA are right on this chip.
No ring or breadcrumb timeouts, nothing poisoned.

## Copies

Microseconds per copy; CPU is the mean of the two off runs.

| Copy | Blitter | CPU | |
|---|---:|---:|---:|
| apart 256x256 | 247.6 | 10,617 | 43x |
| apart 512x200 | 345.0 | 16,378 | 47x |
| scroll up 16, 512x384 | 928.3 | 31,044 | 33x |
| drag +5,+4, 400x300 | 1,446.8 | 19,162 | 13x |
| right 8, 504x384 | 2,451.6 | 44,293 | 18x |
| small 12x12 | 30.2 | 30.2 | declined by the size threshold, so CPU in both |

The CPU path reads uncached video memory at about 6 Mpixel/s; the
blitter does 79 to 297. The overlapping cases (drag, right 8) are the
slowest on the blitter because they go in bands.

## Text

Microseconds per string.

| Font | Blitter | CPU | |
|---|---:|---:|---:|
| System, opaque | 505.4 | 142.1 | 3.6x slower |
| System, transparent | 420.8 | 94.9 | 4.4x slower |
| Arial 16, opaque | 576.2 | 176.4 | 3.3x slower |
| Arial 16, transparent | 498.1 | 116.0 | 4.3x slower |
| Courier New 20 bold, opaque | 1,022.9 | 323.9 | 3.2x slower |
| Courier New 20 bold, transparent | 971.3 | 200.7 | 4.8x slower |

The plan's open question (decision 9) is answered: the CPU expands text
into write-combined memory faster than this path does. Over the run, the
4,114 strings accepted made 4,114 bitmap callbacks and 7,241 rectangle
callbacks, each a submission and a wait. A transparent string is a single
bitmap callback and still costs about 325 us more than the CPU's whole
string; where that time goes (the wait, the DIB Engine's `ExtTextOutExt`
path, the command streamer taking immediate data) was not measured.

## Decision

- Copies stay on by default.
- Text stays on the CPU. The branch turned it off by default; the change
  offered upstream leaves the text path out altogether. It would only be
  worth another look with submissions that do not wait per string, which
  is the asynchronous work the plan leaves out (Trio64 hard lock, Gen3
  erratum 12).

## Not known yet

- The DOS-box round trip with copies on.

Later the same day: the hardware cursor was on and looked right by eye
(`docs/plans/gen3-hardware-cursor.md`), after the configure-order fix
(`docs/issues/2026-11-24-gen3-gdi-and-cursor-off-until-a-mode-change.md`).
A desktop that went wrong and then froze after a while of use, which the
owner had also seen on 0.60.0, was traced to free GDI resources falling
to 0 % at about 5 % a minute once the netbook's Wi-Fi was up, idle or
not, with the copies' counters showing no timeout or poison: a program's
leak, not this driver's.
