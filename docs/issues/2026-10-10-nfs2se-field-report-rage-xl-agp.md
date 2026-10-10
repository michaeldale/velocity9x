# NFS II SE on a reporter's Rage XL AGP: the demo faults in its PowerVR path, the full game ran blind when the render interface was not ready

Date: 2026-10-10. Status: open (cause of NOT_READY not measured).

Source: VOGONS, marxveix, reply 69 of the Velocity9x thread
(<https://www.vogons.org/viewtopic.php?p=1449640#p1449640>), plus the
reporter's `V9XGLIDE.LOG` sent to Michael afterwards (3.4 MB, 0.14.0 and
0.15.0 runs across several boots; not kept in the tree). Reporter's card:
ATI 3D Rage XL AGP `1002:474D` rev 65, 8 MB, desktop 800x600x16,
Velocity9x 0.15.0 `11d348f`.

## What was reported

1. The NFS II SE 3Dfx demo (`C:\GAMES\NFS23DFX\NFS2SEAD.EXE`) faults at
   start: `invalid page fault in module <unknown> at 0000:6c095210`, stack
   holding `.\FeData\pc\Art\Slides\title.qfs`.
2. The full game (`NFS2SEA.EXE`) "did start", the menu was not visible,
   and it faulted at exit in `KERNEL32.DLL at 0187:bff7b9a6`.

## The demo: not Glide

`NFS2SEAD.EXE` is the archive.org NFS2SEA demo's `nfs2sea.exe` (961,536
bytes, 1997-09-04) or the same build: the fault's return addresses
(`0049a804`, `00424274`) land on call sites in that image, and the
census lines match it field for field (`config=00560A04`,
`buffer=006FFC54`).

The faulting instruction is `call [0x562fec]` at `0x49a7fe`, arguments
`(0x562f10, 0)` - the first two stack words in the report. `0x499a06`
fills that pointer: `LoadLibraryA("SGL.DLL")`, then
`GetProcAddress(sgl, "sgltri_isrendercomplete")` into `0x562fec`. SGL.DLL
is the PowerVR SGL library; the demo carries a PowerVR renderer beside
the Glide one ("POWERVR_setvideomode - %d bpp not supported by PowerVR
hardware"). When `LoadLibraryA` fails, `0x499a19` skips the path.

So the reporter's machine has an `SGL.DLL` the demo can load, and the
demo calls into it at `6c095210` after it is gone or never valid there:
Windows reports no module at that address. The Glide log agrees: in every
one of the reporter's demo runs (seven, under 0.14.0 and 0.15.0) the
third load of our GLIDE2X.DLL sees only `grChromakeyMode(0)` -
no `grGlideInit`, `grSstWinOpen` or draw - and the process dies about 2-12 s
later. On A8U4I5 the same demo's third load calls `grGlideInit` 47 ms
after the second and races.

Not tested: a run with an `SGL.DLL` present. The reporter can confirm by
renaming every `SGL.DLL` (WINDOWS\SYSTEM and the demo folder).

## The full game: the render interface was not ready for a whole boot

The reporter's log has two `NFS2SEA.EXE` runs.

- Run 1 (`pid=FFFCC5E1`): `describe result=0 engine=4 generation=7`, about
  190 s of play, 3,195,913 drawn and **604,071 refused** (16 %; every
  logged refusal `result=4 triangles=2 storage/format=0204`), then
  `grGlideShutdown`, `device: closed` and a normal detach. No fault in
  our DLL's lifetime.
- Run 2 (`pid=FFF14DD9`, another boot, `t=97120`): `device: describe result=5 engine=0 generation=0`
  (`V9X_R3D_RESULT_NOT_READY`), so `grSstWinOpen ... -> 0`. The game
  carried on regardless: `grLfbLock` handed it the off-device buffer,
  clears and draws went nowhere (`drawn=0`), about 375 swaps over 80 s,
  and the log ends with no shutdown and no detach - the process died.
  That is the report: "menu not visible", fault at exit.

The same boot's two Pandemonium 2 runs got `result=5` too; a later boot's
Pandemonium 2, Turok and SF Rush runs got `result=0 engine=4`. So NOT_READY
held for that whole boot and not for others.

`NOT_READY` from describe is either `v9x_win16_enter()` failing or
`v9x_r3d_engine()` returning null - for the Mach64 engine,
`v9x_d3d_mach64_ready()` false: no valid framebuffer or engine record, or
the BAR2 MMIO window unmapped (`mobility_hw16.c`). The log cannot say
which.

## A8U4I5, same build (Rage XL PCI `1002:4752`, boot 377)

- Demo from `C:\NFS2DEMO`: opens 640x480, races, everything drawn,
  WM_CLOSE exits cleanly. With 3dfx's reference `glide2x.dll` beside it:
  "_GlideInitEnvironment: glide2x.dll expected Voodoo Graphics, none
  detected", then a clean exit.
- Full game: intro, menu and attract mode draw; WM_CLOSE from the menu
  exits cleanly. Choosing **exit** and pressing Return showed nothing and
  the game idled into attract mode - the menu exit path was not exercised.
  8 `hwtex create 00400040 format=1 hr=8876017C` (out of video memory)
  early on.

## Open

- Why the render interface was NOT_READY for a whole boot on the AGP
  part. Needs a capture from such a boot: the settings report and
  `V9XTRACE` taken while a Glide game shows `result=5`.
- Glide answers a failed `grSstWinOpen` with FXFALSE and an off-device
  LFB, and NFS II SE ignores it. 3dfx's DLL tells the user and the game
  exits. Whether ours should do the same is a design question, not
  decided here.
- The 16 % refusals on run 1 (format `0204`) are not yet matched to a state.
