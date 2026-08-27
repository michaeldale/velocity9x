# Netbook GMA 950 first run: what the on-disk reports say to change

Status: **mostly implemented 2026-08-27** (see the Unreleased CHANGELOG entry).
Items 1, 2, 3 and the item-5 fixes are in the tree; item 4 (heap restriction
masks) remains **open** pending the QEMU measurement it prescribes, and the
weaker fallback (smaller advertised heap) with it.

First run of the `vbe` tier-0 package on a real Intel GMA 950. The machine has
no networking, so nothing was observed live; every statement below comes from
the diagnostic files read off the USB stick afterwards. They are archived at
`claude/personal/v9x-netbook-usb/notes/netbook-boot-20260827/`.

| | |
|---|---|
| Machine | HP Mini 110-1000, Atom N280, Intel 945GSE |
| IGD | `PCI\VEN_8086&DEV_27AE&SUBSYS_308F103C&REV_03` (function 1: `27A6`) |
| Video BIOS | `Intel(r) 82945GM Chipset Family`, VBE 3.0, MD5 `62af31e93cf215ed14153759b6309b47` |
| Package | Velocity9x 0.5.0 vbe, build 3420172 |
| Result | `Stage=enable-ok`, desktop at 1024x576x32, LFB `0xD0000000` |

**The headline is that it worked.** The driver enabled on a chip its family
does not name, read EDID over the VBE path, picked the panel's preferred
1024x576, published six modes, and ran DirectDraw to `Result=COMPLETE` with
every blit path returning `S_OK` and zero engine timeouts or resets across 251
traced callbacks. The items below are what the same files say to improve.

---

## 1. The INF cannot offer this card, and Have Disk had to be forced

`VELOCITY9X.INF` emits one model line, `PCI\VEN_1234&DEV_1111&SUBSYS_11001AF4`.
Windows therefore offered nothing for the IGD, and the install only happened by
picking Have Disk and forcing the one listed model onto a device it does not
match. That works, and `Floppy.HardwareIdHint` in the manifest already says it
is the intended route for other VBE cards, but forcing a mismatched model is a
worse experience than the family already has a mechanism for.

**Use the existing `Inf.ManualSelect` emitter.** `scripts/lib/inf.ps1` already
supports a model line carrying no hardware id, and the `s3` family uses it for
the VLB card. The `vbe` family declares no `ManualSelect` at all. Adding one:

```powershell
Inf = @{
    ...
    ManualSelect = @{
        Description = 'Velocity9x VBE-generic display (any VESA VBE 2.0+ adapter)'
        # Required by the manifest validator (family.ps1 Assert-V9xFamilyKeys):
        # it is the budget Get-V9xFamilyManualSelectModes prunes the manual
        # model's mode list against. 2 MiB keeps every declared vbe mode
        # (largest is 1024x768x16 at 1.5 MiB) while staying honest about
        # small VBE cards.
        VideoMemoryBytes = 2097152
    }
}
```

gives a person an honest entry to choose, keeps the deliberate policy that
Windows never binds this driver by wildcard, and removes the "force a QEMU
model onto an Intel chip" step from `docs/INSTALL.md`.

Do **not** add the Intel ids as ordinary model lines. Two reasons: the tier
cannot claim to drive the 945 family properly, and a real GMA driver family
would then collide with them. If a diagnostic id is ever wanted, `27AE` with
`SUBSYS_308F103C` is the exact string for this machine, and note that the s3
family's comment at `family.psd1:227` records that binding a `CompatibleId` to
a manual model did not do what was hoped there.

Related, lower priority: `Inf.ForcedModes` lists 800x600 and 1024x768, neither
of which this panel can do. The runtime table pruned them correctly, so this is
cosmetic, but the forced list is a QEMU-shaped default sitting in a
chip-agnostic family.

## 2. The driver names itself after QEMU on non-QEMU hardware

`SYSTEM.INI` on the netbook reads:

```
[boot.description]
display.drv=Velocity9x VBE-generic display (QEMU std-vga)
```

That string is `Chips[0].DeviceDesc` in `packaging/families/vbe/family.psd1`,
and it is what Display Properties, Device Manager and `SYSTEM.INI` all show.
On the one machine in the fleet that is not QEMU, the driver claims to be QEMU.

Split the two things the string is currently doing:

- `DeviceDesc` -> `Velocity9x VBE-generic display`. This is the name a user
  sees, and the tier is chip-agnostic by definition, so the name should be too.
- Keep the chip identity in the model line's hardware id, where it belongs, and
  in `Chips[0].Name` (`QEMU/Bochs VBE std-vga`), which is manifest-internal.
- `Chips[0].Adapter` is `QEMU/Bochs VBE (generic VESA linear framebuffer)`.
  This one did not leak on the netbook, because the PCI id did not match and
  `vbe_hw16.c:138` fell through to `Unrecognised card on the generic VBE path`.
  It would leak on any card that did match. Reword it to
  `Generic VESA VBE linear framebuffer`.

Note that `family.ps1:479` enforces `DeviceDesc` uniqueness across chips, so
check the shortened string does not collide before changing it.

While in `vbe_hw16.c:138`: `Unrecognised card on the generic VBE path` is
accurate but reads like a fault. The card is not unrecognised so much as
un-named, which is the whole point of the tier. Something like
`Generic VESA adapter (no chip-specific support)` says the same thing without
implying something went wrong. The `V9XHW.INI` `VendorId=unmatched` /
`DeviceId=unmatched` pair has the same problem: the driver did not fail to read
PCI, it read PCI and found no family entry. `unclaimed` would be clearer, and
recording the actual ids alongside would make the file far more useful when it
is the only thing you get back from a machine with no network.

## 3. Sixteen diagnostic files land in the root of C:

The netbook boot alone left `V9XBOOT.INI`, `V9XDD.INI`, `V9XDDH.INI`,
`V9XHW.INI`, `V9XMODES.INI`, `V9XMSW.INI`, `V9XPWR.INI`, `V9XSNAP.INI` and
`V9XSYNC.INI` in `C:\`, plus `VIDEOROM.BIN` from the survey tool. Across the
whole tool set the count is sixteen distinct root files.

Move them to `C:\V9XDIAG\`. The paths are hardcoded string literals in 20
source files:

| File | Written / read |
|---|---|
| `src/display16/ddi.c:31,193,233,331` | `V9XHW.INI`, `V9XBOOT.INI` |
| `src/display16/enable16.c:259,408,412` | `V9XBOOT.INI` |
| `src/display16/modes16.c:346` | `V9XMODES.INI` |
| `src/display16/dd16.c:57` | `V9XDDH.INI` |
| `src/display16/gdi_accel.c:85` | `V9XHW.INI` |
| `src/display32/ddhal_core.c:77` | `V9XTRACE.INI` |
| `tools/diag/settings_status.c` (26 sites) | reads `V9XHW.INI`, `V9XBOOT.INI`, `V9XMODES.INI`, `V9XSYNC.INI`, `V9XGDI.INI` |
| `tools/diag/settings_syncmodes.c:42,44` | `V9XMODES.INI`, `V9XSYNC.INI` |
| `tools/diag/ddraw_probe_win32.c:18` | `V9XDD.INI` |
| `tools/diag/d3d_trace_dump_win32.c:22` | `V9XSNAP.INI` |
| `tools/diag/mode_switch_win32.c:22` | `V9XMSW.INI` |
| `tools/diag/power_cycle_win32.c:14` | `V9XPWR.INI` |
| `tools/diag/palette_smoke_win32.c:12` | `V9XPAL.INI` |
| `tools/diag/surface_step_win32.c:8` | `V9XSURF.INI` |
| `tools/diag/window_list_win32.c:13` | `V9XWND.INI` |
| `tools/diag/matrox_inventory_win32.c:9` | `V9XMGA.INI` |
| `tools/diag/matrox_mmio_query_win32.c:64,80` | `V9XMGA.INI`, `V9XMGAMM.INI` |
| `tools/diag/vga_survey_dos.c:56` | `V9XSURV.INI` (already has `/out:`) |
| `tools/diag/vbe_inventory_dos.c:33` | `V9XVBE.TXT` |
| `tools/diag/vlb_aperture_dos.c:76` | `V9XAPER.INI` |

Suggested shape: one new header, `include/velocity9x/diagpaths.h`, defining
`V9X_DIAG_DIR "C:\\V9XDIAG\\"` and one macro per file built from it, included
by both bitnesses so the reader and the writer of each file can never drift.
`settings_status.c` alone reads five of them back, so a shared header is the
difference between this working and half of the reports silently going blank.

Three mechanics to get right:

- **Create the directory before the first write.** `WritePrivateProfileString`
  will not create it and fails silently if it is missing, which would turn
  every diagnostic into a no-op. The 32-bit tools can call `CreateDirectoryA`
  and ignore `ERROR_ALREADY_EXISTS`. The 16-bit driver has no Win16 API for it,
  so use INT 21h AH=39h and ignore error 5 (already exists) and error 3. Do it
  once, at the first `V9XBOOT.INI` write in `enable16.c`, before that write.
- **Readers must move with writers.** Covered by the shared header; not covered
  by a search-and-replace across writers alone.
- **The DOS tools write from real mode**, where a subdirectory is equally
  legal, but `vga_survey_dos.c` already defaults to the boot drive and accepts
  `/out:`. Keep that override working: on the netbook it is the only way to
  redirect a report, and `vga_survey_dos.c:2413` prints it as the recovery
  advice when the default path fails.

Documentation that references the old paths and has to move with them: the
package's own `INSTALL.TXT` (`C:\V9XMODES.INI`) and `docs/INSTALL.md`, which
names ten of them across lines 114 to 216.

---

## 4. Video memory is a performance trap on a tier with no blitter

This is the biggest finding and it is not about correctness.

Ironfield 1.2 ran a nine-test suite on this machine under the driver
(`claude/personal/rts/benchmarks/netbook-gma950/`). The only variable that
mattered was where the game staged its frame:

| Staging surface | FPS | Time in "display transfer or flip" |
|---|---|---|
| Video memory | 18.7 to 22.2 | ~28.8 ms/frame, 54 to 64% of session |
| Direct back buffer | 19.7 to 62.2 | ~0.02 ms/frame |
| System memory | **100.5** | 2.9 ms/frame |

Same workload, same driver, same resolution. A 640x480x16 frame is 614,400
bytes, so 28.8 ms is about 21 MB/s. That is a CPU read-back from the uncached
VBE aperture, exactly the cost `src/display32/blt_cpu.c` warns about in its
header, and it is 10x. Simulation was under 2% of session time in every run, so
this is entirely the presentation path.

The driver cannot make the aperture fast, but it can stop handing applications
a surface that is fast to allocate and catastrophic to read:

**Set the heap restriction masks for engineless families.** `dd16.c:377-383`
publishes one linear heap with `ddsCaps = 0` and `ddsCapsAlt = 0`, and the ABI
header at `win9x_ddraw_abi.h:243` states these are restriction masks: what the
heap may **not** be used for. Leaving them zero means the heap accepts
everything, so `DDSCAPS_VIDEOMEMORY | DDSCAPS_OFFSCREENPLAIN` succeeds and the
application gets an uncached surface with no blitter behind it. On a family
whose `EngineType` is `NONE`, setting `ddsCaps` to exclude offscreen plain and
texture surfaces would push those allocations into system memory, where
DirectDraw's own HEL blits them from cached RAM. The primary and the flip chain
still need the heap, so this has to be a restriction and not a zero-sized heap.

This needs verifying against the DDK before shipping: it changes what
`CreateSurface(DDSCAPS_VIDEOMEMORY)` returns, and some applications treat that
failure as fatal rather than falling back. The safe first step is to measure
it, not to assume it. `V9XDD.INI` already records `VideoStageHr` and
`SystemStageHr` separately, so the probe can prove the behaviour change on
QEMU before it is trusted on hardware.

If exclusion turns out to break applications, the weaker version is to stop
advertising a large heap: `GblHalVidMemTotal` came back as `0x00570000`, 5.7 MB
of offscreen VRAM offered on a card where every byte of it is slow to read.

There is also a real fix on the other side, which is a bigger piece of work:
map the aperture write-combining. Nothing in the tree touches MTRRs or the PAT
today. Write-combining would help the write direction and would not fix the
read direction, which is the one that hurts here, so the heap policy is the
better first move.

## 5. Smaller things the reports show

**`V9XDDH.INI` reads as a failure after a healthy session.** The file's only
content is `Stage=setinfo-callback-missing`, written at `dd16.c:416` when
`V9xDdCreateDriverObject` runs before `DDNEWCALLBACKFNS` has supplied SetInfo.
That is a normal transient: the trace in `V9XSNAP.INI` shows
`CountDd16CreateObject=3` and `CountDd16NewCallbackFns=1`, and the session went
on to `Result=COMPLETE`. But `v9x_dd_trace` writes a single last-write-wins key,
so whichever call happened last is what the file claims about the whole boot,
and the last one here was a `DestroyDriver`-adjacent retry. Either append a
sequence (`Stage00=`, `Stage01=`) the way the mode inventory does, or keep a
separate `LastGoodStage` alongside `Stage`. As it stands the file misreports.

**Texture failures use the wrong HRESULT.** `TexSurfaceHr`, `TexHandleHr` and
`TexSwapHr` all came back `0x80004005` (`E_FAIL`) with `TexFormatCount=0`. The
tier genuinely has no texture support, so the honest answer is
`DDERR_UNSUPPORTED`. `E_FAIL` reads as "something broke" in every application
log that will ever quote it.

**`V9XMSW.INI` says `Result=NO-ARGUMENT`.** The mode-switch tool was launched
with no arguments and did nothing. On QEMU that is fine because there is a
command line. On a netbook with no network, reached only by physically carrying
a USB stick to it, a diagnostic that no-ops when double-clicked is a wasted
trip. Give it a default run when invoked bare.

**Hidden-mode reasons are misleading.** `V9XMODES.INI` hid 800x600 and 1024x768
with `hide=scan-contradicted`. The decision was right, and for a good reason:
the panel is 1024x576 and EDID said so. But "scan-contradicted" describes the
mechanism, not the cause, and the cause is knowable here. A distinct
`hide=edid-contradicted` would make these reports self-explaining, which matters
a great deal for a machine you can only read after the fact.

**`VideoMemoryBytes = 4194304` in the manifest is a floor.** The card reported
8,060,928 bytes at 4F00h and the driver used the real figure, so nothing is
wrong. Recording it here only because the manifest comment says the floor is
what the host mode-agreement check lays modes out against, and this is the
first card in the fleet where the two differ by 2x.

## Not defects, recorded so the next reader does not chase them

- **`FlipPixelOk=0`, `Flip20Ms=0`, `FlipMaxMs=0`.** The known, separately
  tracked result. GDI reads the fixed GDI surface, not the flipped page. See
  `docs/issues/2026-08-14-directdraw-hal-nohardware.md` and the note in
  `docs/handoffs/2026-08-27-gdi-accel-trio64-hardware-handoff.md`.
- **`GblNumModes=9` against six published rows**, with `320x200x8` appearing
  twice. Same DDRAW-side merge already recorded in
  `docs/handoffs/2026-08-25-dynamic-vbe-stage2-handoff.md` (33 against 32
  there). Nothing in `dd16.c` produces these rows.
- **`VBlank10Ms=167`** is ten `WaitForVerticalBlank` calls in 167 ms, which is
  16.7 ms each, which is 60 Hz to three digits. The vblank path is correct on
  this hardware.
- **The 36-mode / 6-usable split** matches the earlier DOS survey in
  `claude/personal/v9x-intel950/V9XINTL.TXT` exactly, same six mode numbers
  (0101, 0111, 0112, 0160, 0161, 0162). The 30 zeroed modes are steady state,
  not a scan artifact.
