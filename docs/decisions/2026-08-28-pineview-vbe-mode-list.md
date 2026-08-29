# Pineview lists 36 VBE modes and describes 6

Date: 2026-08-28
Status: **measured three times, agreeing, one of them in real DOS. Settled.**

First third-party report on Intel Pineview. The machine cannot reach its
panel's native resolution under Velocity9x, and could not under SoftGPU or
Bear Windows VBEMP either. This records why, and what the evidence rules out.

Raw capture: `claude\personal\v9x-centaurhauls-acer\source-data\` - the survey
INI, four photographs, and the reporter's notes.

| | |
|---|---|
| Machine | Acer Aspire One, board NAV50, Windows Me (4.90) |
| IGD | `8086:A011` rev 00, subsystem `1025:0349`, class `030000` |
| Second function | `8086:A012`, class `038000` |
| VBIOS | `Intel(r)PineView Graphics Chip Accelerated VGA BIOS`, VBE 3.0 |
| Reported VRAM | 8,323,072 bytes |
| LFB | `0x80000000`, matching BAR2 (prefetchable) |
| Panel | AUO `B101AW03`, 22x13 cm |
| Package | Velocity9x 0.6.0 vbe, build `200cd31` |
| Survey | `V9XSURV /rom`, build `8774305` |

## Finding 1: the BIOS describes six of the thirty-six modes it lists

`VideoModePtr` holds 36 mode numbers, including eighteen Intel OEM numbers
`0x0160`-`0x0171` and six more at `0x013A`/`013C`/`014B`/`014D`/`015A`/`015C`.
`4F01h` returns `0x004F` for every one of them and leaves the ModeInfoBlock
zeroed - attribute bit 0 clear, geometry zero, `PhysBasePtr` zero.

Six modes answer with real data, all with attributes `009B` and
`PhysBasePtr = 0x80000000`:

| Mode | Geometry | Pitch |
|---|---|---|
| `0x0101` | 640x480x8 | 640 |
| `0x0103` | 800x600x8 | 832 |
| `0x0111` | 640x480x16 | 1280 |
| `0x0114` | 800x600x16 | 1600 |
| `0x0112` | 640x480x32 | 2560 |
| `0x0115` | 800x600x32 | 3200 |

**Three independent measurements agree, and two of them are byte-identical.**
The real-DOS survey's 36 mode rows and its 128-byte EDID block `diff` clean
against the Windows Me DOS-box survey - same six described modes, same
geometry, same pitches, same `PhysBasePtr`, same panel. The driver's own boot-time scan
reports `state=1 listed=36 queried=36 cached=6 flags=0107`, and its admission
tally is `2,0,0,0,0,0,0,0,0,0,4,0,0` - two records admitted, four merged as
duplicates of baseline rows, and **every rejection counter zero**. The thirty
undescribed modes never reached `v9x_vbe_scan_accept`; they were dropped as
unsupported at the parse. `flags=0107` is
`CTRL_VALID | LIST_VALID | LIST_TERM | EDID_VALID`, and `QUERY_FAILED`
(`0x0080`) is clear: no query failed.

The arithmetic closes. Seven baseline rows plus the two newly admitted 32-bpp
records is nine, reported as `6 published, 3 hidden`.

An empty ModeInfoBlock with attribute bit 0 clear is the VBE-sanctioned way to
say a mode is in the table but *not available in the current hardware
configuration*. What that configuration is, this report does not say.

## Finding 2: the driver reads the panel correctly and says so

EDID came back valid. Its single detailed timing descriptor:

- pixel clock `0x1450` = **52.00 MHz**
- hactive **1024**, hblank 320, htotal 1344
- vactive **600**, vblank 44, vtotal 644
- so **1024x600 at 60.08 Hz**

Established-timing bytes are `00 00` and every standard-timing slot is `0101`,
so that descriptor is the only thing the panel advertises.

The settings report prints
`EDID recommendation: 1024x600 reason=edid-unpublished`. Per
`src\display16\modes16.c:511` that string is emitted when the preferred
geometry matches no *published* runtime mode. The driver knows what the panel
wants and is reporting that it has nothing to offer for it - which is the
diagnostic working, not failing.

1024x768 is hidden because the scan is sound enough to contradict a baseline
row and EDID caps the panel at 600 lines. 800x600 is therefore the ceiling.

## Finding 3: the duplicate devnode is the second display function

The reporter saw two identical `Velocity9x VBE-generic display` entries in
Device Manager, and had to remove one before the Settings tab would open
without freezing. The survey names the cause: `DisplayDeviceCount=2`, from
`00:02:00 8086:A011` (class `030000`) and `00:02:01 8086:A012`
(class `038000`). The manual-select model line carries no hardware id, so a
forced Have-Disk install attaches to both functions.

This is **not** the unfixed netbook item 1: `ManualSelect` is already declared
for the family at `packaging\families\vbe\family.psd1:171`. The mechanism is
that a hardware-id-less model can be forced onto either display-class function.

`PCI ID: unclaimed:unclaimed` follows from the same route, and with it
`Video memory: Unavailable` and `Clock: Unavailable`.

## What the evidence disputes

**The DOS-box explanation is dead, on evidence.** The first survey was taken
from a DOS box under Windows Me - `WindowsPresent=yes`, `ProtectedOrV86=yes`,
`MachineStatusWord=0019` - and the empty ModeInfoBlocks were attributed to the
VDD virtualizing INT 10h in V86 mode. The driver's own boot-time scan returned
the same six modes by a different path, which weakened that; the real-DOS
survey, with no Windows anywhere and the PE bit clear, returns a mode table
byte-identical to the DOS box's, which ends it. The BIOS behaves the same way
in real mode as it does under a virtualized INT 10h. **This is the video BIOS,
not the environment.**

**The netbook precedent does not transfer.** On the HP Mini's 945GSE
(`8086:27AE`) the panel's native 1024x576 was published as OEM modes
`0x0160`-`0x0162` with a linear framebuffer
(`2026-08-17-intel-gma-phase0-dos-evidence.md`). Pineview lists the same
number range and describes none of it. Same vendor, same OEM numbering,
opposite outcome - so "Intel publishes its panel modes" is a per-BIOS fact,
not a family one.

**It is not a memory limit.** 1024x600x32 needs 2,457,600 bytes against the
8,323,072 the controller reports.

**It is not a Velocity9x defect.** SoftGPU and Bear Windows VBEMP fail the
same way on the same machine, which is what a BIOS that will not describe its
own modes should do to every VBE-generic driver.

## What this licenses, and what it does not

**Both reports are incomplete, in exactly the same way.** Neither carries the
`[Result]` section, which `vga_survey_dos.c` writes unconditionally as the
last thing it does - so neither run reached the end. In both, the
`[VBEModes]`, `[EDID]` and `[VGARegisters]` section headers and their fixed
keys are absent while the indexed rows underneath them (`Mode.NN`,
`Block0.NN`, `Seq.NN`, `Crtc.NN`) are present and well formed.

Identical damage across two independent runs, one under Windows Me and one in
real DOS, is not a transfer accident. It is unexplained, and it is filed as
`docs\issues6-08-28-survey-report-sections-missing.md`. The mode rows and
the EDID block are self-describing and the findings above stand on them, but
nothing here should be read as a complete capture, and the vendor probe never
ran in either.

Nothing here licenses a mode-table patch. The 915resolution route - PAM
unlock, BT_3 table patch in the shadow - was deleted rather than deferred by
`2026-08-17-intel-gma-gen3-hardware-audit.md`, and this report does not
reopen it.

## Next

### What the CRTC override cannot do

The first version of this section proposed VBE 3.0's user-specified CRTC block
as "the standards-sanctioned way to reach a geometry the BIOS does not list".
That is wrong, and worth recording as wrong. The CRTCInfoBlock carries
HorizontalTotal, the two horizontal sync edges, VerticalTotal, the two
vertical sync edges, Flags, PixelClock and RefreshRate - and **no active width
or height**. Bit 11 of BX is documented as selecting user values *for refresh
rate generation*; the active geometry still comes from the mode number. It
buys a custom refresh at a resolution the BIOS already has, which is not the
problem here.

There is no VBE 3.0 specification in this tree or in the DDK to cite, so that
is stated from the specification rather than from a document under version
control. It is cheap to check in passing on the HP Mini 110.

The block builder was written anyway and is host-tested against this panel's
EDID (`src\common\vbe_crtc.c`, `tests\host\test_vbe_crtc.c`). It earns its
place if a 1024x600 mode number turns out to exist, because it can then drive
the panel at its own 52.00 MHz instead of whatever the BIOS would pick.

### The sweep

What is left is to find out whether any of the eighteen OEM numbers *is*
1024x600, and the only thing that can answer is setting one and looking.

`V9xMini_Vbe_Sweep` (`src\minivdd32\loader.asm`) does that inside the existing
Device_Init collection: for every listed mode the query pass could not
describe, `4F02h` it with the linear and no-clear bits, then `4F01h` again -
because a BIOS that refuses to describe a mode cold may describe the active
one. A record that comes back usable enters the same cache as any other and
the host-tested admit rules judge it on content, so a BIOS that behaves this
way publishes its panel mode with no further change. The entry mode is
captured with `4F03h` first and put back at the end; a BIOS that will not say
what mode it is in is not swept at all.

**It is off by default.** `4F02h` is a heavier call than the collection's
`4F00h`/`4F01h`, it runs the real BIOS with no timeout, and it changes the
display mid-boot - so it is assembled in only by
`build-minivdd-skeleton.ps1 -ModeSweep`, plumbed through
`build-active-package.ps1 -ModeSweep`, and the image audit refuses a build
whose symbol presence disagrees with the switch. Every attempt is named on the
wire before it is made, so a boot that dies names the mode that killed it.

### Validate it where the answer is known first

The HP Mini 110's 945GSE **does** describe `0x0160`-`0x0162` as 1024x576
(`2026-08-17-intel-gma-phase0-dos-evidence.md`). That makes it the right place
to run the sweep first: the modes it would sweep are ones `4F01h` already
answers for, so the mechanism can be checked against ground truth before it is
ever pointed at a BIOS that stays silent.

---

## 2026-08-29: the sweep ran on the machine, and rescued nothing

Collection: `claude\personal\v9x-centaurhauls-acer\` - `V9XDIAG` from the
Pineview machine running **`b8c5103-sweep`**, the set-and-ask-again build this
finding motivated.

`VbeCache=s=53504 l=36 q=36 c=6 p=0 f=2107`. `f` carries `SWEEP_RAN` (0x2000),
so the sweep executed. The outcome is `listed=36 queried=36 cached=6` - the
same six modes the BIOS describes without it, and the same six the query-only
DOS survey gets: `0101, 0103, 0111, 0112, 0114, 0115`.

**It tried all of them.** Thirty modes were undescribed, `V9X_STAGE1_SWEEP_MAX`
is 32, `V9X_VBE_CACHE_MAX` is 64 and `CACHE_FULL` is clear, so no cap stopped
the walk before the end. Setting a mode and asking again does not make this
BIOS describe it.

*Inferred from the bounds, not read: the sweep's own counter `V9xVbeSwept` is
not published in `VbeCache=`, so "it swept all thirty" cannot be distinguished
from "it swept fewer and the caps happen to allow thirty". Publishing that
counter is a one-field change worth making before the next collection.*

The DOS survey in the same collection shows the shape plainly - thirty entries
like the first and six like the last:

```
Mode.00=0160,0000,0,0,0,0,0,0,00000000,0,0,0,0,0,0,0
Mode.35=0111,009B,640,480,1,16,6,1280,80000000,1280,5,11,6,5,5,0
```

The undescribed thirty are the OEM range `0160`-`0171`, `013A/013C/014B/014D/`
`015A/015C`, and standard `0105/0107/0117/0118/011A/011B`. `QUERY_FAILED` stays
clear because the BIOS answers `004F` and hands back a zeroed block: it
succeeds and describes nothing, so our admit rules reject on content.

### Consequences

- **The panel's native mode stays unreachable through the BIOS.**
  `Edid=v=0103 preferred=1024x600` and `Recommendation=1024x600
  reason=edid-unpublished`: we know what the panel wants and have no admissible
  mode that provides it. The machine runs 800x600 scaled.
- **The sweep should not be run on this machine again.** It buys nothing here
  and costs thirty blind `4F02h` calls into that BIOS at every `Device_Init` -
  see `docs\issues\2026-08-29-pineview-driver-will-not-load-after-survey.md`,
  where a boot that did not come back is the leading reading of a field report.
- What is left for 1024x600 is chip-specific: a Pineview backend that programs
  the hardware instead of asking its BIOS. One VBE-legal long shot remains
  untested - `4F06h` to force bytes-per-scanline plus VBE 3.0's `4F02h` bit 11
  user-CRTC block on a mode whose *format* is already known - and it risks a
  blanked panel, so it wants scoping before anyone tries it.
