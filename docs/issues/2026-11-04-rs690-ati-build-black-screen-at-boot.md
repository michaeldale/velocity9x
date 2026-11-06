# RS690: the first ati-family build black-screens at boot; the owner rolled back to VBEMP

Date: 2026-11-04

Machine: M-GB8KRE (ATI RS690 IGP, `1002:791F`), Windows 98 SE, remote help.
Build: the `ati` package from branch `ati-rs690` at `13103c2d` with the
DirectDraw hookup uncommitted (`rs690_hw16.c`, `eng_rs690.c`, the mini-VDD's
`RS690_MMIO_MAP`, 002Bh); `run-checks` passed on it.

## What happened

1. The package went to `C:\V9XATI` (29 files, every hash matched) and was
   installed over the signed 0.34.0 `vbe` driver by Device Manager's Have
   Disk. The one model offered was "Velocity9x ATI Radeon X1200 (RS690)";
   the restart prompt was declined. `WININIT.INI` held renames for
   `glide2x.dll`, `glide3x.dll` and `v9xdisp.drv` and no `NUL=` line.
2. `reset` at about 07:03Z. The agent did not reconnect for over three
   minutes.
3. The owner wrote at 07:13Z: "black screen on reboot with last driver",
   then "had to boot into safe mode to rectify".
4. At 07:15Z the machine was back with the owner's earlier driver,
   Bearwindows' VBEMP: `Display\0001` names `JWSOFT~1.INF`, section
   `PCIVID`. `V9xFamily=ati` and `V9xSyncGeneration=54` are left in that key
   from the install. The `ati` package's `V9XDISP.DRV`, `V9XHAL.DLL` and
   `V9XMINI.VXD` are in `C:\WINDOWS\SYSTEM` (hashes match the package) but
   unused.

## Evidence

None from the failed boot. `V9XBOOT.INI`, `V9XHW.INI`, `V9XDDH.INI` and the
other reports in `C:\V9XDIAG` are the last `vbe` session's at 1024x768x16:
`V9XHW.INI` names "Generic VESA adapter" and `vbe-generic-unavailable-v1`,
strings the `ati` driver does not contain. So the `ati` driver failed, or
the machine stopped, before its Enable wrote a report. Whether the screen
went black at the mode set, at Enable, or in the mini-VDD's init is not
known.

## Hypotheses, none tested

- **The family's gamma hook.** `ati_hw16.c`'s `load_gamma` writes the
  Mach64 DAC ports at register offsets 4C0h-4C4h through whatever window a
  chip's engine hook maps. For the RS690 that is the Radeon register file,
  where those offsets mean something else. It runs only when GDI sets a ramp
  above 8 bpp (`ddi.c`); whether that happens at boot was not checked. The
  Rage 128 and Evergreen chips take the same path and boot.
- **Something in the ati family image that assumes a Mach64**, run for every
  device before the engine hook: the mini-VDD is built with
  `V9X_ATI_MOBILITY_MMIO` for the whole family.
- **The new map itself** (002Bh) or the 16-bit hook's BAR read, both run
  during Enable.

## Next

Reproduce on a machine where a hang costs no one: the ati package with the
RS690 row cannot be tested without a 791F, so the first step is reading the
family image's boot path for chip-agnostic Mach64 accesses, then a build
that records each Enable stage before the engine hook runs. M-GB8KRE is
used again only with the owner's agreement.

## 2026-11-04, later: narrowed without the RS690

- **The shared image boots elsewhere.** The same build's `V9XDISP.DRV`,
  `V9XMINI.VXD` and `V9XHAL.DLL` (hashes matching the package that went to
  M-GB8KRE) were put on the 86Box Mach64 VT2 guest by `WININIT` rename and
  booted it: boot 140, uptime 21.6 s at the first check, `Stage=enable-ok`
  at 1024x768x16, `V9XHW.INI` naming the VT2. So the family image,
  `runtime.asm`'s new thunk and the mini-VDD's new function do not stop a
  boot by themselves. The guest was put back on its own binaries the same
  way and shut down.
- **Ruled out by reading.** GDI acceleration claims only engine types it
  has code for, so the RS690 type never goes live there (`gdi_accel.c`).
  The gamma hook runs only when an application sets a ramp above 8 bpp,
  which a 640x480x8 first boot cannot. Everything the mini-VDD builds for
  this family alone runs on request, not at init. DriverInit and the
  display component start touch no device.
- **What the stale `Stage=` does not prove.** The failed boot left
  `Stage=enable-ok` from the `vbe` session, which first suggested it never
  reached GDI's first query (`query-start`). That is not safe: the profile
  flush puts the marker in the file, but VCACHE writes it to disk lazily,
  and a hang with interrupts off stops the lazy writer before the owner's
  power-cycle. So the Enable-time path specific to this chip - the 64-bit
  BAR read (`V9xPciReadEgBars`), the map (002Bh), the descriptor's
  consumers - is still in play.

## Next, needing the RS690

A bisection, each step a boot on M-GB8KRE: (1) the 791F row with no engine
hook, tier-0 under the `ati` family; (2) the hook with the BAR read and no
map; (3) the full hook. Markers that survive a hard hang would make one
boot enough: a DOS commit (INT 21h 6800h) after each `Stage=` write, or a
scratch register on the chip read back after a warm reset. Each step risks
another safe-mode recovery by the owner.

## 2026-11-04, evening: test boots on M-GB8KRE (Michael: "you can continue doing test boots")

Each row is one `reset` after a Have Disk install or a `WININIT` rename;
"hang" means no agent reconnect and the owner reporting a black screen,
then restoring a driver from safe mode. Boot numbers are the agent's.

| # | Driver key | V9XDISP.DRV | V9XMINI.VXD | Instrument | Result |
|---|---|---|---|---|---|
| 1 | ati (Have Disk) | ati, hook | ati | none | hang |
| 2 | ati (Have Disk) | ati, hook | ati | trail A | hang, trail empty |
| 3 | vbe (Have Disk, tree build) | vbe | vbe | trail A | **boots** (boot 18), trail full |
| 4 | vbe | ati, hook | ati | trail A | hang, trail empty |
| 5 | vbe | ati, hook | vbe | trail B | hang, trail empty |
| 6 | vbe | ati, hook | vbe | trail C | hang, trail empty |
| 7 | vbe | ati, no hook | vbe | trail C | hang, trail empty |
| 8 | vbe | ati, no hook | vbe | trail D | hang, trail empty |
| 9 | vbe | vbe | vbe | trail D | **hang**, trail empty |

Trail A appends a committed line at each `Stage=` write (first at
`libmain`). B adds a line as DriverInit's first statement and around each
step of `v9x_modes16_init`. C adds a disk reset (INT 21h 0Dh) and a two
second wait after every line. D writes each line into a slot of a
pre-sized 4 KiB file instead of appending. B, C and D all filled their
files on the 86Box Mach64 VT2 guest.

What holds:
- Row 3: the current tree's `vbe` package boots this chip, so nothing on
  main since 0.34.0 hangs it through the `vbe` install.
- Rows 1, 2 and 4: the `ati` binaries hang it, under either driver key, so
  the INF install path is not the cause.

What does not: row 9 shows instrument B-D itself hangs this machine with
the `vbe` driver, which boots without it. So rows 5-8 prove nothing - not
the display-driver-versus-VxD split, not the hookless result - and their
empty trails say nothing about where the `ati` driver stops. Which part of
B-D hangs (the first-statement marker, the markers between mini-VDD calls,
the disk resets or the waits) was not separated.

Eight black screens (rows 1, 2 and 4-9) cost the owner eight safe-mode
recoveries. Test boots are paused.

## 2026-11-04, the clean split (Michael: "Clean split, few boots")

The instrument went back to trail A, the form row 3 booted with: one
appended, committed line per `Stage=` write and nothing else. Both boots
ran under the owner's `vbe` driver key with the tree-built `vbe` VxD.

| # | V9XDISP.DRV | Result |
|---|---|---|
| 10 | ati, hook | hang; owner recovered (boot 25) |
| 11 | ati, no hook (the RS690 row tier-0) | hang; owner recovered from `C:\V9XVBE` (boot 26: `BOOTLOG.PRV` records the safe boot, the driver key's date is the tree build's, and the installed driver matches that folder's copy) |

Row 11 first read as a boot - the agent reconnected inside `reset`'s wait
and a trail-A trail appeared - but the driver that wrote it was the
`vbe` one the owner had reinstalled, not the staged `ati` one.

So:
- **The fault is in the `ati` display driver** (row 10: a known-good VxD).
- **It is not the RS690 engine hook** (row 11): the `ati` family's path
  hangs this chip with the row tier-0, while the `vbe` family's boots it
  and the same `ati` driver boots the Mach64 VT2.

What separates the two families for this chip with no hook: the family
device list (one PCI scan per id), `pci_match_optional` (0 for `ati`, 1
for `vbe`), the `ati` diagnostics publisher and memory hook, and the
`vbe` family's `V9X_D3D_DEFAULT_REQUEST=2`. The mode rows are identical.
Not yet narrowed, and no trail evidence places the hang before or after
DriverInit.

The machine was put back on the signed 0.34.0 files by rename and booted
(boot 27). Ten black screens in all.

## 2026-11-04, later: the mini-VDD decides it, not the display driver

Boots after the clean split. Every row stages files by `WININIT` rename
under the `vbe` driver key; HAL is the signed 0.34.0 one throughout.
"signed VxD" is the 0.34.0 `vbe` `V9XMINI.VXD` (build id `d9d7aeb9`);
"tree VxD" is one assembled on this branch (build id `13103c2d-dirty`).

| Boot | V9XDISP.DRV | V9XMINI.VXD | Result |
|---|---|---|---|
| 28 | released 0.34.0 `ati` (no 791F row) | signed | boots, `fail-hardware-present`, VGA fallback |
| 30 | `ati`, no hook, `V9X_D3D_DEFAULT_REQUEST=2` | signed | boots, `Adapter=ATI Radeon X1200 (RS690)`, `enable-ok` |
| 31 | same, SYSTEM.INI `[Velocity9x] Direct3D=0` | signed | boots; live switches to 1024x768x16, 640x480x8, 1280x1024x32 pass |
| 32 | same | signed | boots at 1024x768x16 |
| 33 | `ati`, no hook (61,498 bytes, the row-11 binary) | signed | boots |
| 35 | same, after a `vbe` session | signed | boots |
| - | `ati`, hook, with the `ati` VxD and HAL | tree `ati` | hang |
| 37 | `ati`, hook | tree `vbe` | hang (owner recovered; safe boot in `BOOTLOG.PRV`) |
| 39 | `ati`, hook | signed (not replaced) | **boots**, map refused, `EngineCaps=0` |
| 40 | `ati`, hook (installed) | tree `vbe`, only file staged | hang |
| 41 | `ati`, hook | 0.34.0 source, id `13103c2d-dirty` | hang |
| 42 | `ati`, hook | signed, renamed over itself | **boots** |
| 43 | `ati`, hook (installed) | 0.34.0 source, id `13103c2d-a12` | **boots** |

Boot numbers are those the agent reports once the machine is up (after a
hang, the recovered boot). Each hang cost the owner a safe-mode
recovery; they restored the signed files each time.

What this shows:
- Rows 30-35 were not "the hookless build booting in a better state", as
  first read. They all ran with the signed VxD, and row 11 (the same
  61,498-byte binary, hang) ran with a tree VxD. The state-dependence
  conclusion is withdrawn.
- With the display driver held fixed (the hooked `ati` build), the VxD file
  alone decides the boot: signed boots (39), tree hangs (40).
- Not the rename itself: the signed VxD renamed over itself boots (42).
- Not the source. Built here from the `v0.34.0` tag with id `d9d7aeb9`, the
  VxD is byte-identical to the signed one (SHA-256 `F9635789...`). The same
  source with id `13103c2d-dirty` hangs (41).
- **Alignment, not offset.** The id is assembled into nine strings in
  `_LDATA` (`V9xMiniVddBuildId` at 50h, then the COM1 lines), so each extra
  id character moves all later locked data nine bytes. Eight characters
  (signed, `_LDATA` F7Ch) boots; fourteen (+36h, data at 2 mod 4) hangs;
  twelve (+24h, still dword aligned, `_LDATA` FA0h) boots (43).

So some locked VxD data that this display driver's path depends on stops
working when it sits at 2 mod 4. Every release id is eight hex digits,
which is why no shipped build shows it; every `-dirty` development build
of the VxD is misaligned. It needs the `ati` display driver with the 791F
row: the same tree `vbe` VxD boots this chip under the `vbe` display
driver (row 3, its id not rechecked), and the tree `ati` VxD boots the
86Box Mach64 VT2. Which variable is involved and why a 2-byte shift
matters on x86 is not known. Reading the VxD for `test reg, 3`, page
masks, `rep movsd` counts and data addresses handed to the display driver
found nothing yet.

The trail never recorded a line from a hung boot, not even `libmain`. It
recorded every booted one, so either the hang comes before LibMain's first
trace, or a committed append does not survive a hang. Not separated.

## Next

1. Read the `ati` display driver's path for the 791F row against the VxD
   API: which verbs it calls that the `vbe` display driver does not, and
   what each reads from locked data.
2. Pad the id include to a multiple of four in
   `build-minivdd-skeleton.ps1` (or `ALIGN 4` after it). That only covers
   the symptom, and is not a substitute for item 1.
3. Until then, RS690 test builds need a VxD whose id keeps the data
   aligned (eight or twelve characters), or the signed VxD.

Done on the branch for testing (Michael: "Seems ok to pad for this gpu
while we test"): `ALIGN 4` after the id include in `loader.asm`, so the
locked data after it is dword aligned whatever the id. `run-checks`
passed. Not yet booted on M-GB8KRE: the machine went offline at 11:25Z,
right after the restore, before the padded `ati` build could be staged.

Booted 2026-11-04 17:25Z, when the machine returned (boot 46, no
leftover `WININIT.INI`, signed files installed). All three `ati`
binaries of the padded build (DRV 61,772 bytes with the hook, VxD
14,436, HAL 945,664, built a minute after the pad) were staged by rename
and the machine restarted with `reset`. It did not come back: no agent
check-in in the 33 minutes after the reset. A hang, pending the owner's
recovery; no evidence read yet.

So padding the id does not make the full hooked build boot. Either the
pad does not cover what the misalignment broke (the `ati` VxD is a
different image from the `vbe` one the id-length boots used), or this
combination has a second fault of its own: the map (002Bh) and the HAL
using the engine never ran in any booting row. Both are open. The
cheaper split, once it is back: the padded `ati` VxD with the signed
DRV and HAL, which tests the pad alone.

## 2026-11-05 test series

| Step | DRV | VxD | HAL | Result |
|---|---|---|---|---|
| C | `ati`, hook, padded build | `ati`, padded | `ati` | hang (17:25Z, after boot 46); owner restored the signed files, back 23:26Z at boot 48, all three hashes signed, no `WININIT.INI` |
| A | signed | `ati`, padded | signed | hang (2026-11-05 10:30Z `reset` after boot 48): the rename ran (`WININIT.BAK` holds the one VxD line, staged file gone), `BOOTLOG.PRV` shows a safe boot, the owner put the signed VxD back (hash F963…), boot 49 normal at 640x480 |
| A2 | signed | `ati`, tree, `-BuildId 13103c2d` (8 chars), hash DE42DE36… | signed | boots (10:37Z `reset`, boot 50, `Stage=enable-ok`, no safe boot) |
| B | `ati`, hook | `ati`, padded | signed | dropped: A hung |
| B′ | `ati`, hook (the padded build's DRV, B2FC3CC2…) | A2's | signed | boots (10:44Z `reset`, boot 51, `Stage=enable-ok`); `V9XHW.INI`: Adapter=ATI Radeon X1200 (RS690), Acceleration=directdraw-fill-copy, EngineCaps=00000003, so the 002Bh map works |
| C′ | as B′ | A2's | `ati` (padded build's HAL, 5F1112A1…) | boots (10:46Z `reset`, boot 52); all three hashes as staged; `V9XHW.INI` as B′ |

C′ differs from the hung C only in the VxD (8-character id instead of
the padded 14). So the hook, the 002Bh map and the HAL's engine use
are not the fault: the whole black-screen series is the mini-VDD's
build-id-dependent layout.

V9XDDP (0.34.0 copy in C:\V9XPKG) on C′, 10:48Z: `Result=COMPLETE`,
`V9XDDH.INI` Stage=setinfo-ok. BltFillPixelOk=1, SrcCopyPixelOk=1, all
four overlap copies PixelOk=1, StretchBlt and source colour key Hr=0;
FlipHr=0 but FlipPixelOk=0 (flip not yet on D1GRPH). Captures in the
session scratchpad, not yet in `docs/probe/`.

Series closed 10:52Z: signed 0.34.0 files renamed back, `reset`, boot 53
on the signed hashes (VBE generic). Owner recoveries in all: about
fifteen.

Open, for a decision: how to keep the VxD layout independent of the id.
The `ALIGN 4` pad did nothing and should come out. Candidates: a
fixed-width id field in `V9XBUILD.INC` (every id padded or cut to one
length, so `-dirty` builds lay out like clean ones), or first find the
mechanism (disassemble the `V9xMini_Mtrr_Write` crossing sites and read
back what the loader left there on a hanging layout, which costs hangs).

Decided 2026-11-05 (Michael: fixed-width id field). The `ALIGN 4` pad
is out of `loader.asm`. `build-minivdd-skeleton.ps1` now stamps the
VxD with an 8-character id: `-dirty` dropped, longer ids cut, shorter
ones padded with `_`. The display driver and HAL keep the full id. Built
with `13103c2d` and with `13103c2d-dirty`, the `ati` VxD is
byte-identical to A2, the image that booted at boot 50. `minivdd-local`
gives the same layout and differs only in the 72 id bytes. `run-checks`
passed on the change, and the rebuilt `build\win98se-ati\V9XMINI.VXD`
is A2 byte for byte.

What this does not do: source changes still move the layout. On this
tree the `s3`, `intel-gma` and `nvidia` VxDs each carry one or two
page-crossing fixups, the same as their clean builds. If the crossing
correlation is the real cause, a future code change could bring the
hang back on the RS690 with any id. The fix only guarantees that a
dirty test build and the clean commit it becomes have the same layout.

A hanging on its own says the `ALIGN 4` pad does not rescue the `ati`
VxD; the 14-character-id `vbe` rows were the only evidence for the
alignment reading. The pad does sit after every id string (the
locked data after it is dword aligned in A), so misalignment of that
data is not the mechanism. A2 shows the `ati` VxD itself boots here
with an 8-character id.

What does separate the six VxDs booted on this machine so far is the
LE fixup layout. `LINK /VXD` writes 512-byte LE pages, and a 32-bit
fixup that straddles a page edge is written as two records (+509..+511
in one page, -3..-1 in the next). Counted with a scratch parser:

| VxD | id | Result | Page-crossing fixups (object offset, routine) |
|---|---|---|---|
| signed 0.34.0 (d9d7aeb9) | 8 | boots | none |
| 0.34.0 tag, 12-char id (boot 43) | 12 | boots | none |
| A2 | 8 | boots | none |
| 0.34.0 tag, 14-char id (boots 40, 41) | 14 | hang | 19FEh, `V9xMini_Mtrr_Write` +17Ch |
| tree `vbe`, `13103c2d-dirty` | 14 | hang | 19FDh, `V9xMini_Mtrr_Write` +155h |
| A (padded `ati`, 14) | 14 | hang | 15FFh `MiniVDD_PM_API` +32Ch, 1DFDh `V9xMini_Mtrr_Write` +6Ch |

`V9xMini_Mtrr_Write` programs real MTRRs, which this Sempron has and
the 86Box guests do not exercise the same way, which would fit a
machine-specific hang. But crossings are ordinary: padding the 14-char
`ati` build by 4 to 80 bytes in steps of four never produced a layout
without one, and Windows' own VxDs from the same linker must carry
them. So this is a correlation, not a mechanism; nothing here shows the
loader mishandling them. A bootable layout needs a disassembly of the
crossing sites against what the loader wrote, or a VxD with a crossing
deliberately moved into a routine that does not run. Until then RS690
test VxDs use an id with a layout already seen to boot (8 characters).

The machine was left on the signed 0.34.0 files (all three hashes
checked, `Stage=enable-ok`). About fourteen black screens in all.
