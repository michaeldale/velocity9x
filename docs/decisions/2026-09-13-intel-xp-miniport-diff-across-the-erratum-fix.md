# The XP miniport across the erratum-fix boundary: 4864 removes render-clock switching; erratum 12 is not visible in strings

Status: research note, 2026-09-13. The Phase 4 errata gate
(`docs/decisions/2026-09-12-intel-phase4-errata-gate.md`) stays closed.

## The three builds

All three are the `igxp` codebase, so they diff like against like. Versions
read from the file version resources, not the package names.

| Role | `igxpmp32.sys` | Date | Package |
|---|---|---|---|
| Before the fix | 6.14.10.4814 | 30 Mar 2007 | Dell A05, INF restricted to Dell `27A2` subsystem ids |
| **The fix build named by Intel** | **6.14.10.4864** | 24 Aug 2007 | Dell package, INF also restricted to Dell `27A2` ids |
| After the fix, control | 6.14.10.4885 | 30 Oct 2007 | Intel generic, INF lists `27AE` as 945GM/GME |

Intel's spec update 309220-0132 names PV 14.31.1.4864 as the first Windows XP
driver carrying the erratum 12 workaround; that is file version 6.14.10.4864.
So 4814 to 4864 brackets the fix exactly, and 4864 to 4885 is a control: a
change that is the workaround should appear in the first diff and persist
through the second. Neither Dell INF lists `27AE`; the miniport code path is
shared across the 945 family, so this does not affect the comparison.

## What the strings say

Printable and UTF-16 strings, six characters or longer, set-differenced.
4814 to 4864: 932 added, 875 removed. 4864 to 4885: 719 added, 685 removed.

**Removed at 4864 and never returning: the whole render-clock frequency
switching subsystem.** Every `Gsv*RCFrequency*`, `GsvAlmadorRC*`,
`GsvNapaRC*`, `GsvGen4SWFreq*`, `CamarilloRenderPStateSwitch*` and
`IsTimeOut(): Exceeded Render Geyserville Idle Time-Out` string is gone,
along with `GsvAlmadorRCFrequencyControl(): Reading/Writing PCI Register
Failed`. Napa is the 945GM platform; Geyserville is Intel's dynamic frequency
scaling; "Render Clock Frequency Control" switching the render clock between
two frequencies is Dual-Frequency Graphics Technology. Intel's published
workaround for **erratum 7** is "disable DFGT in the graphics driver". The
4864 build did not disable it; it deleted the code. That is a clean match for
erratum 7 and explains why 4864 is the fix build for the DFGT hang.

**Nothing in the strings matches erratum 12.** No new flush, buffer,
coherency or chipset string arrives at 4864 and stays. The only "flush"
change is a removal: "Chipset is Springdale. Flush and wait for 15 micro-sec"
leaves with the 865 support. Two new strings mention PCI configuration reads,
both for VT-d detection (`PCIReadCfgReg failure during VTd detection!`,
`Unsupported GGMS value encountered during VTd detection!`), which is
Cantiga-generation work, not a 945 fix. One new string says "Dynamic normal
watermark programming is disabled in WA file", confirming the miniport keys
workarounds off a per-device table rather than named code paths.

## Assessment

- Erratum 7 is now understood at the driver level: the fix is the absence of
  render-clock switching. Velocity9x never touches the render clock, so the
  Phase 4 sandbox is on the right side of erratum 7 by construction, and the
  plan's "AC only" caution can be reconsidered once that register state is
  measured.
- Erratum 12 remains undisclosed. If its workaround is in 4864 it is a WA
  table entry plus a small code path, invisible to strings. Finding it means
  disassembly of `igxpmp32.sys`: the configuration-space access sites
  (`VideoPortGetBusData`, `VideoPortSetBusData`, or the driver's own
  `PCIReadCfgReg`) compared across 4814 and 4864 for a new offset, and the WA
  table's entries for device `27A2`/`27AE` revision 3.
- The flush-page lead (`2026-09-12-intel-flush-page-lead.md`) is unchanged by
  this: no string in any of the three builds names D0:F0 `60h` or a flush
  page.

## Gate consequence

None. The gate opens on a workaround that can be cited or implemented, not on
knowing which erratum a build fixed. This note narrows the search: erratum 7
is accounted for, so whatever else 4864 changed in the 945 path is the
candidate set for erratum 12.

Raw packages are under `C:\temp\intelcompare\{Old,Other,New}` on the build
host, not in the repository.
