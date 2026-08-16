# Three tier-0 backend defects

Status: **D1, D2 and D3 all fixed 2026-08-16** on this branch, for merge into
`vbe-tier0`. All three were found while designing the ATI Mach64 family; they
live in chip-agnostic code belonging to the tier-0 backend rather than in
anything ATI-specific.

D1 and D2 were blocking for stage 4, the first install on the physical laptop.

**Measured effect of the fixes:**

| Family | D1 (`CR69`) | D2 (VRAM floor) |
|---|---|---|
| `s3` | declares `FLIP` - **unchanged**, still writes CR69 | non-NULL `read_aperture`, never reaches the tier-0 default - **no effect** |
| `matrox-m2` | no caps - **stops** writing S3 CR69 on a Matrox | non-NULL `read_aperture` - **no effect** |
| `vbe` | no caps - **stops** writing S3 CR69 on std-vga | applies |
| `ati` | no caps - never writes CR69 | applies |

S3's `publish_diagnostics` lives in `s3_regs16.c` and never references the VRAM
variables, so its `V9XHW.INI` output is unchanged.

Code cost: **+54 bytes** on the S3, VBE and ATI images and +56 on Matrox, all
from the shared `enable16.c` change. Well inside the 2 KiB per-step budget.

**One caveat worth stating.** The Matrox behaviour change is a fix - writing an
S3 extension register on an MGA-2164W was never right - but `matrox-m2` is
real-hardware-only and this has not been run on that card. `V9xHalFlip` will now
decline a primary flip there rather than report success it could not deliver.

---

## D1 - `set_display_start` writes an S3 extension register on any chip

`src\display32\engines\vga_scanout.c:34-45` programs the display start through
VGA `CR0C`/`CR0D` **and S3 extension register `CR69`**. It is called from three
places:

- `V9xHalFlip` - `src\display32\ddhal_core.c:313`
- `V9xHalFlipToGDISurface` - `ddhal_core.c:407`
- `V9xHalSetExclusiveMode` - `ddhal_core.c:435`

None of them go through the engine vtable, and none are gated on `engine_type` or
on `V9X_DD_ENGINE_CAP_FLIP`. Grepping confirms `V9X_DD_ENGINE_CAP_FLIP` and
`_VBLANK` appear **only** in the two S3 chip modules and the manifests - nothing
in the driver reads them, so they currently mean nothing.

**Impact today:** the shipping `vbe` tier-0 package already writes S3 `CR69` on
QEMU std-vga hardware.

**Impact on the ATI work:** it would write an undefined register on an ATI part,
on a laptop whose only display is an internal LCD with no recovery path.

**Fix**, already anticipated by `docs\plans\multi-chip-restructure.md:52` -
*"`set_display_start`, `in_vblank` and `build_caps` join the table when the files
move"*: move `set_display_start` and `in_vblank` into `V9X_ENGINE32_OPS` and have
the three call sites do nothing when `v9x_engine32()` returns null. A cheaper
interim fix is to gate the three call sites on
`engine.engine_caps & V9X_DD_ENGINE_CAP_FLIP`, which would finally give that
capability bit a meaning.

**Until this is fixed, the `ati` family must not claim `FLIP` or `VBLANK`.**

---

## D2 - VRAM arithmetic underflows when the BIOS reports less memory than the mode needs

`src\display16\dd16.c` takes `vram_bytes` verbatim from the tier-0 default
(`v9x_vbe_default_aperture()` in `enable16.c:85`, clamped only *downward* against
the mapping). When a BIOS under-reports, three things break:

| Site | Breakage |
|---|---|
| `dd16.c:257-259` | `fpStart = base + visible_bytes`, `fpEnd = base + vram_bytes - 1`. At 1024x768x16 with a 512 KiB report, `fpStart > fpEnd` - a heap whose end precedes its start. |
| `dd16.c:266-267` | `dwVidMemTotal = vram_bytes - visible_bytes` on `DWORD`s **underflows to ~4.29 GB**. DirectDraw is told there are 4 GB of off-screen video memory past the end of the framebuffer. |
| `ddhal_core.c:605-608` | Every blit rectangle is bounds-checked against `fb.vram_bytes`. At 640x480x16 (614,400 bytes visible) that is already larger than 512 KiB, so even CPU blits to the bottom of the primary would be refused. |

**This is not hypothetical on our hardware.** The Rage Mobility's VBE reports
`TotalMemory64K = 8` (512 KiB) on a panel running 1024x768x16.

**Fix:** keep the raw BIOS answer for the `V9XHW.INI` `VbeVramBytes` diagnostic -
"this BIOS claims 512 KiB" is exactly what a bug report from an untested card
needs - but floor the *usable* value at `visible_bytes`, rounded up to the next
64 KiB. Roughly 20 bytes of code in `enable16.c`.

Prefer `visible_bytes` over falling through to `dd16.c`'s existing 4 MiB
fallback: on an unknown card, believing in off-screen memory you have not proven
exists hands DirectDraw surfaces that alias the visible framebuffer. Tier-0 does
CPU blits and barely uses the off-screen heap, so "no off-screen memory" costs
almost nothing, whereas a wrong ceiling costs corruption.

**Regression gate:** the S3 and Matrox families never set the variable, so their
`V9XHW.INI` and `DDGETTRACE` output must be **byte-identical** after the change.

**Follow-on to check:** when `vram_bytes == visible_bytes` the heap is empty
(`fpEnd == fpStart - 1`, `dwVidMemTotal == 0`). Confirm DirectDraw copes; if not,
set `dwNumHeaps = 0` in that case rather than inventing memory.

---

## D3 - "other VBE 2.0 cards via Have-Disk" does not actually work

**FIXED 2026-08-16.** Option 1 and option 2 were both rejected in favour of a
narrower change: the `vbe` family sets a new `pci_match_optional` flag in its
`v9x_hw16` table, and `enable16.c` stage 1 treats a PCI miss as fatal only for
families that do not set it. The scan still runs - it is what decides whose
hooks execute - but tier-0 no longer vetoes a card the INF never claimed to
match, which is the whole point of a Have-Disk override.

Every other family leaves the flag zero, including `ati`, which is itself
tier-0: the generic package is the single answer to "my card is not listed", and
a vendor package accepting anything would publish its vendor's identity for a
card that is not one. The field sits last in the struct so that zero - the
strict behaviour - is also what an initializer that forgets it gets.

`v9x_vbe_publish_diagnostics` now reports `unmatched` for the adapter and ids
when `v9x_pci_match` is `0xFFFF`, rather than the fallback entry's std-vga
strings. Rationale and the rejected alternatives are recorded in
`docs\decisions\2026-08-16-vbe-tier0-family.md`.

Code cost: **+34 bytes** on `s3`, `matrox-m2` and `ati` - the shared
`enable16.c` condition and the struct field - and **+150** on `vbe`, which also
carries the diagnostics check and its two strings. Inside the 2 KiB per-step
budget.

**Not yet re-measured on hardware.** The original failure was observed on the
86Box Mach64 VT2 guest; the fix has been built and audited for all four families
but not re-run there. That re-run is the outstanding verification.

The original finding follows.

**Measured 2026-08-16** on the `Win98SE-Mach64VT2` guest (86Box Mach64 VT2,
`1002:5654`), by doing exactly what the manifest advertises.

`packaging\families\vbe\family.psd1` says:

> `HardwareIdHint = 'PCI 1234:1111; other VBE 2.0 cards via Have-Disk'`

and the backend registry comment says unlisted cards "reach tier-0 by
Have-Disk". The Have-Disk install itself works: Windows offers
"Velocity9x VBE-generic display (QEMU std-vga)", warns that the driver was not
written for this hardware, accepts it, copies the files, and binds the class to
`v9xdisp.drv` / `v9xmini.vxd`.

**Then the driver refuses to enable.** `C:\V9XBOOT.INI` reads:

```
Stage=fail-hardware-present
```

which is `ddi.c:613` - `V9xHardwarePresent()` returning 0, serial
`stage=device-id`. The cause is that a family carries **two independent
allowlists**, and Have-Disk only satisfies one of them:

| Allowlist | Where | Satisfied by Have-Disk? |
|---|---|---|
| INF hardware ids | generated `[Velocity9x.Models]` | **No** - Have-Disk bypasses the match entirely, which is the point of the mismatch warning |
| Runtime PCI device list | `v9x_hw16.devices[]`, stamped into DGROUP by `ddi.c` and scanned by `V9xFindPciDevice` | **No** - it is compiled into `v9xdisp.drv` and never consulted the INF |

So Have-Disk gets you a bound-but-inert driver on any card the family does not
name. The card falls back to VGA and the only evidence is a stage code.

This is not academic: it is the documented route for owners of untested cards,
and it cannot work for any of them.

**Options**, none of which should be chosen without a decision record:

1. Say so honestly - change `HardwareIdHint` to state that tier-0 requires the
   card's PCI id to be added to the family, and that Have-Disk alone is not
   enough. Smallest change, and it stops the docs promising something false.
2. Give the `vbe` family a wildcard runtime device entry (say vendor `0xFFFF`)
   that `V9xFindPciDevice` treats as "match anything". This makes the advertised
   behaviour real, but it deliberately reintroduces the catch-all that
   `backend_registry.c:21-26` argues against - though note the argument there is
   about the *backend registry*, which is a different allowlist from this one.
3. Read the accepted hardware id out of the registry at Enable time rather than
   compiling it in.

**A second, smaller finding from the same run.** Hot-swapping the `ati`
binaries onto that vbe-bound class with `update-associated-driver.ps1` produced
`Stage=query-ok` - the driver was loaded and its GDIINFO queried, but Enable
never ran, so the guest stayed on VGA. That is correct behaviour rather than a
bug: the script's own scope is `already-associated-driver-only`, and the
registry association still carried the vbe INF's `MatchingDeviceId` of
`1234:1111`. Swapping the binary does not re-associate the device. The `ati`
family has to be installed through its **own** INF.

## Why these are not ATI bugs

Both are reached by any family whose `read_aperture` hook is NULL - which is the
definition of tier-0 - and D1 is reached by every family regardless. The ATI port
is simply the first hardware to make them visible. Fixing them on `ati-mach64`
would leave the `vbe` family broken and create a merge conflict in shared files.
