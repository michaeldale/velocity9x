# Carmageddon II exits after its intro video on a reporter's GMA 900: D3D draws and then quits, Glide refuses 93 % of its draws

Date: 2026-10-10. Status: open. Since 0.16.0 the game exits earlier, in
both renderers, before the intro video. The 2 GB RAM hypothesis is dead
(reporter, 2026-10-11, below); the cause is unknown.

Source: five field reports through `V9XUPD.EXE /REPORT`, one reporter,
2026-10-10 21:34-21:59 server time: V9X-XJE6R5, V9X-D4P5MY,
V9X-21GQ16, V9X-3D8STZ, V9X-VCWHZ1. No contact address given.

Reporter's machine: Lenovo laptop, Intel GMA 900 (915GM)
`8086:2592` SUBSYS `17AA:2062` rev 04, function 1 `8086:2792` on the
stock "Standard PCI Graphics Adapter" (problem 22, as expected).
Windows 98 SE 4.10.2222 A, DirectX 4.08.01.0901, 512 MB RAM, desktop
1024x768x16, VBE-reported VRAM 8,060,928 bytes. Velocity9x 0.15.0
`11d348f`, all modules agreeing. The game is `CARMA2_HW.EXE`.

## What was reported

"Carmageddon II terminates after intro video", four times with Direct3D
and once with Glide ("Black screen during video").

## Which report is which

| Code | Boot (guest clock) | Renderer | Files received |
|---|---|---|---|
| V9X-XJE6R5 | 21:47:04 | Glide then D3D in one boot | V9XSNAP.INI, cut to 16384 |
| V9X-D4P5MY | 22:03:31 | D3D | V9XSNA1.INI, cut to 16384 |
| V9X-21GQ16 | 22:08:20 | D3D | V9XSNA2.INI, cut to 16384 |
| V9X-3D8STZ | 22:08:20 | D3D | V9XSNA3.INI whole, BOOT, HW, stale GL log |
| V9X-VCWHZ1 | 22:14:03 | Glide | V9XSNA4.INI whole, BOOT, HW, stale GL log |

21GQ16 and 3D8STZ are the same boot and the same counters; 3D8STZ has
no description.

## Direct3D: the game draws for about 7 s, then shuts down in order

From V9XSNA3.INI (D4P5MY's counters agree within a run's length):

- `D3dContextCreates=1`, `D3dTextureCreates=546`, `TexturePlaced=546`
  (2,932,736 bytes), `I9xxDrawsSubmitted=2385`, `I9xxDrawsRefused=0`.
- 640x480x16 target, `FlipRingIssued=166`, `FlipTakenAtDone=163`,
  first draw at tick 71989, last at 78943.
- Frame cover: `Cover1Drawn=3840`, box `0,48 632,424` - pixels reached
  the back buffer.
- The trace ring ends with every surface destroyed, `FlipToGDISurface`,
  `D3dContextDestroyAll`, a fresh `Dd16CreateObject`,
  `SetExclusiveMode(0)` and `Dd16DestroyDriver`. No `V9XTRACE.INI`
  fault capture in any report.

So the driver did not fault and refused nothing. The game reached 3D
rendering and chose to exit. What it asked for just before is not in the
counters: DirectDraw runtime answers (available video memory, caps) are
not recorded. Candidates, none tested: a video-memory query it judges
too small (the stale Quake 2 log on the same machine has
`hwtex create ... hr=8876017C`, DDERR_OUTOFVIDEOMEMORY, at 8 MB), or a
surface or format it requests after the front end.

## Glide: draws refused at Gen3 reason 6, nothing on screen

From V9XSNA4.INI (VCWHZ1): `d3d-contexts=0 gl-describes=1`,
`R3dDrawCalls=3904`, `I9xxDrawsSubmitted=256`, `I9xxDrawsRefused=3645`,
`I9xxRefuseLast=6`, `I9xxTextureDraws=0`. Every frame-cover sample has
`Drawn=0`. The shutdown sequence is the same as the D3D runs.

Reason 6 is `V9X_I9XX_REFUSE_VERTICES`: the vertex run builder rejected
the stream before ring submission (`src/display32/d3d/d3d_i9xx.c`, the
`v9x_i9xx_build_*_run` calls). Which vertices, and whether the video is
drawn as triangles or through the LFB, needs the reporter's
`V9XGLIDE.LOG` (38,152 bytes on their disk at 22:14:28, per
`[DiagFiles]`). XJE6R5 shows the same signature
(`I9xxDrawsRefused=13365`, `I9xxRefuseLast=6`) in its Glide attempt.

## Report tooling defects found on the way

1. `V9XUPD.EXE` did not offer `V9XGLIDE.LOG`; it sent `V9XGL.LOG`, a
   Quake 2 session from 22:02 that says nothing about this game. Fixed in
   source 2026-10-10 (`tools/diag/update_win32.c`, report file list);
   untested against the server, which may refuse the name - a refused
   file is skipped and the rest still go.
2. The first three uploads arrived as exactly 16384 bytes, cut mid-key,
   with no BOOT/HW files, so the server shows no adapter. The same files
   on the reporter's disk were 31,584 / 31,633 / 31,683 bytes
   (`[DiagFiles]` in V9XSNA4.INI). 16 KB is the client's send chunk
   (`tools/diag/update_net_win32.c`, `v9x_net_send_all`), and the server
   stored the partial body as complete. Cause not established; the two
   later uploads of 31 KB arrived whole.

## Next

- Ask the reporter for `C:\V9XDIAG\V9XGLIDE.LOG`.
- Reproduce on MICHAEL-NETBOOK (945GSE, the same Gen3 engine). The game
  is not yet installed on any test machine.
- For the D3D exit: record the DirectDraw answers the game sees (a
  `GetAvailableVidMem` or caps census) before guessing.

## 2026-10-11: the report path fixed in source so the next report is usable

Gates: `run-checks.ps1` green, including the new host test. None of
this has run on a guest or reached the server yet.

1. **The trace ring is no longer flushed by a teardown.** The HAL now
   counts a repeat on the entry already there instead of appending it
   (`include/velocity9x/trace_fold.h`, `src/common/trace_fold.c`,
   `tests/host/test_trace_fold.c`, written failing first). A
   DestroySurface pair repeated 546 times takes four entries, not 1,092,
   so the 56-entry ring keeps what came before the shutdown. A pair keeps
   an enter count and an exit count and folds only while they match, so
   a call that never returned still shows. The count lives in bits 6-14
   of the entry id; `V9XTRACE.EXE` prints it as ` x<n>`, the fault flush
   as ` times=<hex>`. The 16-bit writer does not fold and writes a count
   of zero.
2. **The report client sends the Glide logs.** `V9XGLIDE.LOG` and
   `V9XGLD3.LOG` are in the upload list.
3. **Logs say when.** The attach line of `V9XGL.LOG`, `V9XGLIDE.LOG` and
   `V9XGLD3.LOG` now carries `time=` (local clock) and `uptime-ms=`, so
   a session can be matched to a snapshot's `DumpTime` and
   `DumpUptimeMs` and a stale one is visible as stale.
4. **A non-blocking send that would block is retried.** The Winsock path
   treated `WSAEWOULDBLOCK` after a writable `select` as failure, in
   16 KB pieces against Win9x's 8 KB send buffer. That fits the three
   truncated uploads: the first piece accepted, the second refused, the
   connection closed. It is the likeliest cause, not a measured one: the
   reporter's machine may have used WinInet. Pieces are now 4 KB, a
   would-block waits and retries, and a stall with no progress for the
   30 s timeout fails.
5. **A failed upload says where it stopped.** The transport, stage,
   error code and body bytes sent are written to `V9XUPD.INI` as
   `LastSendFailure=` (sent with the next report) and shown in the error
   message.
6. **The server refuses a short body.** `reports.php` (bluetraitblog
   v9x_update_checker plugin) answers 400 when fewer bytes arrive than
   `Content-Length` declared, and stores nothing, so a dropped upload can
   no longer become a report cut mid-line. That also makes the client's
   Winsock retry after a failed WinInet send safe. `php -l` clean; not
   exercised by a request, and not deployed.

## 2026-10-11: measured on the 86Box ViRGE/DX guest (Win86SE)

Build `dd9c44f` (s3 package) deployed by WININIT rename, host port 9879.

- **Server.** It was already deployed: editing the plugin folder in
  `C:\everything\bluetraitblog` reached production. A request declaring
  31,584 bytes and delivering 16,384 got `400`, "Incomplete upload of
  V9XSNAP.INI: 16384 of 31584 bytes arrived.", and no report appeared.
- **Trace ring, first rule.** After `V9XDDP` (725 DestroySurface, 35,779
  Flip) and `V9XTRACE`, the ring showed `DestroySurface ... x3` folded.
  It also showed the rule's flaw: `GetDriverInfo enter 0xFFAA7540 x2`,
  `exit 0`, `exit 0x88760028`. Folding the enter before its exit put a
  second GUID against the first GUID's result. Superseded: a pair now
  folds only when the new call's exit is in and matches; the tentative
  enter is appended and taken back out on a fold. Host test updated with
  this case.
- **Trace ring, revised rule** (HAL 387,584 bytes, boot 659): the same
  run reads `GetDriverInfo enter 0x7DE41F80`, `exit 0`, then
  `GetDriverInfo enter 0x3B8A0466 x2`, `exit 0x88760028 x2` - the two
  declined GUIDs folded, the accepted one on its own - and
  `DestroySurface ... x3`. Limitation seen: 36 of the 56 entries were
  alternating Lock/Unlock pairs, which do not fold, since only a repeat
  of the same call does.
- **Upload, success.** `V9XUPD /REPORT` (new build) sent V9X-85KJH1:
  7 files, 587,675 bytes, the snapshot at its full 33,971 bytes. WinInet
  carried it; the Winsock retry path did not run.
- **Upload, failure.** `/SERVER=` pointed at a host fixture that reads
  20,000 bytes and resets. The fixture saw a WinInet POST (20,668 bytes
  received), then a Winsock POST (20,703). The client stopped with
  "V9XSNA7.INI: winsock send error 10054, 24576 of 33971 body bytes sent"
  and wrote the same to `V9XUPD.INI` `LastSendFailure=`. "Sent" counts
  what the guest stack accepted, not what arrived. Only the last
  transport's failure is kept.
- **Not reproduced:** the original 16 KB truncation. This guest sends by
  WinInet, so the old Winsock would-block path was not exercised; the
  cause of the reporter's truncation stays a hypothesis.

## 2026-10-11: reproduced on MICHAEL-NETBOOK (945GSE) in Glide mode

Carmageddon II installed by Michael; build 71318ed plus the working
tree, boots 131-141.

- **Glide not offered:** `carma2.exe` offers 3DFX (Glide) only if it can
  load `glide2x.dll`. The netbook had been updated by WININIT renames
  only, so the INF never put the DLL in SYSTEM. Installed by hand; the
  launcher then offered and selected it. Not a driver defect.
- **The reporter's refusal reproduced:** 682,745 of 688,201 triangles
  refused (99.2 %), Gen3 reason 6. Corners logged on refusal showed the
  untextured quads' oow as -1.97, 0 and the bytes "ombi": unwritten. A
  Voodoo reads oow only for texturing, the W-buffer and table fog; rhw
  is now 1 where none applies. Refused fell to 6 in 1.4 million.
- **The intro draws:** HAL frame captures (V9XTRACE -arm) at 30 s and
  70 s show the video - road, car, smoke - drawn as textured tiles.
- **The menu stayed black**, and each fix below was found from the log
  and a capture, not from the screen alone:
  - `guColorCombineFunction`, `grConstantColorValue(4)` and `grHints`
    were stubs; the game sets its colour combine, constant colour and
    GR_STWHINT_W_DIFF_TMU0 only through them. Written.
  - Alpha SCALE_OTHER by TEXTURE_ALPHA (the menu text's glyph mask)
    was unknown; mapped, and an alpha nothing reads no longer makes a
    draw textured.
  - REPLACE beside the fragment's alpha on RGB565 was refused by Gen3
    as UNSUPPORTED; with the alpha unread it is now DECAL.
- **Still black, the cause measured:** the game downloads several
  textures to TMU address 0 (64x64 ARGB4444, 64x64 RGB565, 4x4 and 8x8
  glyphs) and sources earlier ones again without downloading them.
  The DLL keeps whole textures, not bytes, so a source whose texture was
  overwritten finds nothing: 1.46 million textured draws skipped by
  207 s. Keeping a partly overwritten texture (committed) was not
  enough. Next: TMU memory as bytes, decoded per source.
- **Separate defect:** about 300 s into a run every lock and flip fails
  with DDERR_SURFACELOST (887601C2) and the DLL never restores its
  surfaces. Seen twice (runs 1 and 8). Not yet investigated.
- Exiting the game: `WCLOSE.EXE CARMA2_HW.ICD` closes the window but
  the process stays; only a reboot clears it.

## 2026-10-11: on 0.16.0 the reporter's game exits before the video, in both renderers

Source: V9X-DWFJKP (Glide, 20:24 server time) and V9X-1TKP9Q (D3D,
20:28), same reporter and machine, Velocity9x 0.16.0 `bd63c52` installed
by `V9XUPD` over 0.15.0 (`State=committed`), all modules agreeing.
Reported as "terminates before intro video". The Glide log was read
from the full download (`C:\temp\reports1`), because the report MCP
returns only the first 500,000 characters of a file and the session is
at the end of it.

- **D3D, against 0.15.0's V9X-3D8STZ.** The same 546 textures are
  created and placed (2,932,736 bytes, none refused). On 0.15.0, 481
  DrawPrimitives calls then submitted 2,385 textured batches over 7 s
  and the frame cover drew pixels. On 0.16.0 there are 2 DrawPrimitives
  calls carrying state only (`StateMaxCount=81`, `DpRecords=0`,
  `TextureHandleSets=2`), 4 flips and 10 colour fills, then the same
  orderly teardown 1.3 s after the first flip. `I9xxDrawsSubmitted=0`.
- **Glide, pid FFFD1B35.** `grSstWinOpen` 640x480, 3 buffers, `-> 1`.
  The game clears and swaps four times, downloads no texture, sources
  none, never locks the LFB, and the DLL detaches 61 ms after the fourth
  swap with no `grSstWinClose` or `grGlideShutdown`. Five earlier
  `CARMA2_HW` sessions on this machine (20:22-20:34) are identical to
  the line. On the netbook the intro is drawn after this point.
- **Both renderers stop at the same place:** after the blank frames
  before the intro, before its first frame. The D3D run never loads
  `GLIDE2X.DLL` into `CARMA2_HW.EXE` (only the launcher attaches), so the
  Glide-only defect below cannot be the common cause.
- **No fault capture.** The HAL's unhandled-exception filter writes
  `V9XTRACE.INI`; `[DiagFiles]` lists none. Either the game quit on
  purpose, or it replaced the filter (unrecorded).
- **What did change on the machine:** `[System] MemoryKB`
  (`GlobalMemoryStatus` total physical) is 523,808 in every 0.15.0
  report and 2,088,348 in both 0.16.0 reports. The MTRRs are identical
  and already cover 2 GB (`Mtrr1=7f800000 ff800800`), so the RAM was
  fitted all along and Windows was limited to 512 MB before, likely by
  `MaxPhysPage`. Windows 98 above 512 MB is a known source of failures,
  and memory arithmetic in signed 32-bit values fails near 2 GB. That
  this game does so is a hypothesis, not a measurement.
- **Driver changes 0.15.0 -> 0.16.0 on this path:** trace folding and a
  TSC check in `ddhal_core.c`; the rest is Glide, Glide 3 and Matrox.
  Not ruled out, but not implicated by anything in the counters.
- **Separate Glide defect, not the cause:** both clears into the front
  buffer (`grRenderBuffer(0)`) are refused, `clear refused: result=1`
  (`V9X_R3D_RESULT_INVALID`, `src/glide/glide_core.c`). The screen is
  black either way.

Not recorded anywhere, and needed to close this: why the process ended
(exit code, caller, any message box), whether the game replaced the
fault filter, and what memory the process was told it had.

Next: ask the reporter to set `MaxPhysPage=20000` under `[386Enh]` in
`SYSTEM.INI` and run both renderers again.

**Answered 2026-10-11, hypothesis killed.** On
[GitHub issue #5](https://github.com/michaeldale/velocity9x/issues/5)
the reporter says they limited RAM to 512 MB with `burnmem.sys` (which
explains `MemoryKB=523808` in every 0.15.0 report), then ran without it,
then with `MaxPhysPage`: the same exit each way. No report was submitted
from the limited runs, so this rests on their word, not a snapshot. The
reporter also stresses that 0.15.0 got further (past the video) than
0.16.0 (before it). What is still unexplained: what changed between the
two releases that ends both renderers after about four frames, when the
D3D process never loads `GLIDE2X.DLL`.

## 2026-10-11: the HAL now records how each program ends

`src/display32/ddhal_watch.c`, with the import walk in
`src/common/pe_import.c` (host test `tests/host/test_pe_import.c`,
written failing first). The records go in `V9X_D3D_DIAGNOSTICS.watch`
(shared ABI 2026101101) and `V9XTRACE` writes them as `Watch*` keys:

- `Watch<n>*` for each of the last four processes: name, pid, image
  base, which imports were replaced (`Hooks`, bits in
  `win9x_ddraw_abi.h`), whether it ended (`Ended`: 1 unloaded the HAL, 2
  exited), and `GlobalMemoryStatus` as that process saw it.
- `WatchExit*`: `ExitProcess`, or `TerminateProcess` of the caller
  itself. The code, and the return address as caller and module base;
  their difference is an offset to disassemble.
- `WatchMessage*`: caption, text and caller of the latest `MessageBoxA`,
  recorded before the box is shown.
- `WatchFilter*` / `WatchFault*`: a program's `SetUnhandledExceptionFilter`
  is kept beneath the HAL's instead of replacing it, so the fault is
  recorded (and `V9XTRACE.INI` written) and the program's filter still
  runs.
- `V9XGLIDE.LOG` / `V9XGLD3.LOG` detach lines now say
  `process exiting=0|1`.

What the guest showed while building it (86Box ViRGE/DX, Win98SE, s3
package `603c75c-dirty`, boots 661-663, probe `V9XWATP.EXE`):

- **The HAL never attaches to a game.** It is a shared-arena DLL;
  DirectDraw loads it once, in DDHELP, and other processes call into it
  without loading it. The first build hooked `DLL_PROCESS_ATTACH` and
  watched only `DDHELP.EXE` (boot 661). The watch now starts at a
  process's first CreateSurface, Direct3D context or render-interface
  describe, by loading `V9XHAL.DLL` into that process. That also keeps
  the HAL mapped for the life of the process, so a replaced import slot
  never points at unmapped code.
- **Windows 9x reuses process ids.** Probes ended by TerminateProcess or
  a fault never detach, and `V9XDDP.EXE` was skipped as "already watched"
  (boot 662). Whether a process is watched is now whether the HAL is
  loaded in it (`GetModuleHandleA`). Boot 663: `V9XDDP` watched,
  `Ended=2`, and the next probe received its pid `0xFFFD8785`.
- **Each path, measured:** `exit` recorded `ExitCode=0x1234` and caller
  `0x00401209`, which is the instruction after
  `call [__imp__ExitProcess]` in the probe's object (offset 0x209 from
  the image's 0x401000 code base). `terminate` recorded `0x2345`, with
  `Ended=0` because TerminateProcess skips DLL detach. `message` recorded
  the caption and text. `fault` recorded `0xC0000005` at `0x0040132C` in
  the probe image and the probe's own filter still ran. `unload`
  released DirectDraw, freed `DDRAW.DLL` and still exited through the
  hook without a fault.
- **Regression:** `V9XDDP.EXE` under the watch, `Result=COMPLETE`, 957
  keys, no failure values; 5 Direct3D contexts.

Not tested: a real game, the Glide detach line (no Glide program was run),
any other chipset, and a C runtime DLL's exit path, which the watch does
not see (the detach record still says the process ended).
`message` mode's self-dismissal did not fire under the agent's hidden
launch; the record was complete, and the box was not on screen afterwards.
