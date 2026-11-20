# The HD 3000 in 3DMark 2001 SE, vsync on and "Always off"

Date: 2026-11-20
Status: **measured once each, M-NZHWBR boot 64.** Build `dc48a9b4-dirty`
(main `31ab9aee` carries the same code). Evidence:
`docs\probe\m-nzhwbr-snb-hd3000-3dmark2001-vsync-2026-11-20\`, with the
runner `snb-3dm2001.ps1` (the HD 4600's `hsw-3dm2001.ps1` and
`vsync-run.ps1` for the relay). No capture of this machine's output exists,
so tearing and flicker on the monitor are not seen: the counters and the
frame grabs are the evidence.

## How

3DMark 2001 SE's default benchmark with the owner's saved settings:
1024x768x32, Low detail, software T&L ("not run using default settings").
V9XTRACE snapshots either side; run 1 with ten frame grabs.

- Run 1: SYSTEM.INI has no `[Velocity9x] VSync=`, so the default, vsync on.
- Run 2: `VSync=2`, "Always off", written before the run. SYSTEM.INI was
  put back afterwards, the same bytes as found, and is not kept here.

## Results

| | Run 1, vsync default | Run 2, vsync off |
|---|---:|---:|
| 3DMark 2001 SE | **2126** | 2613 (diagnostic) |
| Minutes to the score | 5.0 | 5.1 |
| Flips handled | 6,897 | 9,898 |
| `FlipVSyncOverridden` / `FlipUnsyncedHeld` | 0 / 0 | 9,898 / 9,898 |
| `FlipTakenAtDone` | +6,880 | +17 |
| `DrawsIntoPresented` | +0 | **+13** |
| Engine idle timeouts, resets | 0, 0 | 0, 0 |
| `BatchesEngineRefused`, `MipTreeDeclined` | +0, +0 | +0, +0 |

Run 1's grabs (`contact-sheet.png`): the loading screen and nine Car Chase
frames, the trucks, the robot, explosions, fog and lighting and the HUD
drawn as expected at contact-sheet size.

## Reading

- **Vsync on** paces every flip to the display, all taken when the plane
  took them.
- **Vsync off runs the Haswell path's unsynced flips**
  (`2026-11-19-haswell-vsync-off-async-flips.md`), which Sandy Bridge
  shares through `hsw_scanout.c` and the Ivy Bridge plane bit: every
  unsynced flip held until taken, and copy-present rather than the async
  fallback. The flip counters say which: the async fallback writes the
  base on every flip and counts each one taken or not at done (about
  9,900 here); copy-present writes it once, onto the copy target, and then
  the plane does not move. Run 2's `FlipTakenAtDone` rose by 17, as the
  HD 4600's copy-present runs G and H rose by 6 and 4. `TimeBltCopyCalls`
  does not separate them: it times DirectDraw's own blits, not the
  present copy.
- **Open: `DrawsIntoPresented` +13 with vsync off.** On the HD 4600 it
  stayed 0 through runs G and H. What it counts under copy-present is the
  draws into the buffer last flipped to, which the plane is not scanning;
  whether these 13 can show on screen is not established, and with no
  capture it cannot be looked at here.

## Not measured

- The monitor, with vsync off: whether the frames tear once and whole, as
  copy-present did on the HD 4600's capture.
- 3DMark 2001 SE at other settings.
