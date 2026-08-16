# Two tier-0 backend defects, deferred to `vbe-tier0`

Status: **open, deliberately not fixed here.** Found 2026-08-16 while designing
the ATI Mach64 family. Both live in chip-agnostic code that belongs to the tier-0
backend, so they should be fixed on the `vbe-tier0` branch and merged forward,
not patched on `ati-mach64`. Michael's call, 2026-08-16: hold until the
`vbe-tier0` session is finished.

Both are **blocking for stage 4** (first install on the physical laptop). Neither
blocks stages 1-3.

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

## Why these are not ATI bugs

Both are reached by any family whose `read_aperture` hook is NULL - which is the
definition of tier-0 - and D1 is reached by every family regardless. The ATI port
is simply the first hardware to make them visible. Fixing them on `ati-mach64`
would leave the `vbe` family broken and create a merge conflict in shared files.
