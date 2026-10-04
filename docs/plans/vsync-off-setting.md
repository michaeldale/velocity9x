# A settings-page switch to turn vsync off, for every chip

Date: 2026-10-04. Status: Built the same day, phases 0-3 done and phase 4
done on A8U4I5 only (`docs/decisions/2026-10-04-vsync-setting.md`).
Michael chose three values (Game decides, Always on, Always off).
Decisions 1 and 3 were not answered and the recommendations were taken.
Requested by Michael the same day: one setting in the Velocity9x page of
Display Properties that disables vsync on every driver, chip and
presentation path.

As built, the key is `VSync=` with 0 = Game decides, 1 = Always on and
2 = Always off. This differs from the `VSync=0`-is-off sketch below, so
that a typo cannot turn vsync off. The control is a three-entry combo on
the Active mode row, not a checkbox on the Mode switching row; that row's
value text is too long to share.

## What already exists

Every wait for the vertical blank is in the 32-bit HAL. The 16-bit driver,
the mini-VDD and the OpenGL ICD wait for none.

| Path | Where | Waits for the blank today |
|------|-------|---------------------------|
| DirectDraw Flip, base written in the blank (Intel register path, Mach64) | `ddhal_core.c:814-826`, `v9x_scanout_flip_window_open` | Yes, Flip returns WASSTILLDRAWING until the window opens |
| DirectDraw Flip completion | `v9x_flip_arm` `ddhal_core.c:603`, `v9x_flip_done` 639 | Yes, the flip stays pending until the retrace |
| GetFlipStatus | `V9xHalGetFlipStatus` `ddhal_core.c:1069` | Yes, and this is what holds applications to the refresh rate (comment at 395-418) |
| Flip issued while one is pending | `ddhal_core.c:785` | Yes, refused until the last one is taken |
| Draws while a flip is pending | `d3d_i9xx.c:2224`, `d3d_virge.c:1694` (`v9x_flip_wait_done`) | Yes, on Intel and ViRGE only |
| WaitForVerticalBlank | `ddhal_core.c:1837` | Yes, it is an explicit request from the application |
| Intel ring flip (MI_DISPLAY_FLIP) | `i9xx_scanout.c`, `i9xx_flip.c` | The hardware applies it at the retrace, whatever the HAL does |
| OpenGL SwapBuffers | `gl_surface.c:568` `v9x_gl_drawable_present` | No. It blits to the primary with `DDBLT_WAIT`, which waits for the blitter only |
| GDI | - | No |

Two facts shape the design:

- **A no-vsync path already exists.** `DDFLIP_NOVSYNC` from the application
  skips both the window wait (814) and the pending state (`v9x_flip_arm(1)`
  leaves the machine idle). The setting only has to supply the same answer
  when the application did not ask for it.
- **That path has probably never run.** `dwCaps2` is never set, so
  `DDCAPS2_FLIPNOVSYNC` is not advertised (`ddhal_core.c:2008`,
  `dd16.c:875`). Whether DDRAW forwards `DDFLIP_NOVSYNC` without the cap
  has not been checked. It must be treated as untested code.

OpenGL already presents without vsync, and so do Matrox and VBE, which have
no FLIP cap and fall back to DirectDraw's copy. On those, the setting
changes only the paths in decision 1.

## The constraint that shapes all of this

The standing rule from 2026-09-18 is no INI keys for new features
(`docs/plans/write-combining-stage-b.md`, `opengl-1.1-icd.md`). This is not
a feature behind a switch. The switch is what Michael asked for, and it
belongs on the settings page beside `Direct3D=` and `HighColor=`. Its
default is today's behaviour. It does not override the rule for anything
else.

The page has no height to spare. Its height sets the size of the whole
Display Properties dialog, which already overruns the 211-unit budget for
640x480 at 222 (`settings_propsheet.rc:4-22`). A new row is not
available.

## The design

Settings follow the existing route. The panel writes SYSTEM.INI
`[Velocity9x]`. The 16-bit driver reads it, a pure module resolves it, and
the result is stamped into the shared block for the HAL. The HAL reads no
INI files and must not start now.

- **Key.** `VSync=` in `[Velocity9x]`. `0` means off. A missing key or any
  other value means the application decides, as today. Unknown values fall
  back to the default, so a typo cannot turn vsync off.
- **Policy, `src/common/vsync.c` and `include/velocity9x/vsync.h`.**
  Host-tested, C89, no Windows headers.
  - `v9x_vsync_resolve(requested)` maps the INI integer to a policy value.
  - `v9x_vsync_flip_novsync(policy, flip_flags)` answers "does this flip
    skip the blank": true when the application said NOVSYNC or the policy
    is off.

  This is the `d3dmode.c` pattern.
- **Stamp.** A new policy bit, `V9X_DD_ENGINE_CAP_VSYNC_OFF` (0x800), in
  `engine_caps`. It is set in BOTH places that build that word:
  `v9x_dd_stamp_engine_caps` and `v9x_dd_refresh_framebuffer`. The
  `D3DSoftSysMem` bug of 2026-09-10 was a bit stamped in only one of them
  (`dd16.c:681-697`). `v9x_dd_refresh_framebuffer` runs at every DirectDraw
  session setup, so the setting should take effect when the next DirectDraw
  or Direct3D program starts, with no restart. Phase 0 checks that.

  A policy bit is not a capability. It can only remove a wait, never grant
  a flip the chip cannot do. The 16-bit driver remains the only authority
  on capability (`dd16.c:619-629`).
- **HAL.** `v9x_flip_body` computes `novsync` once from the policy call and
  uses it at both gates: the window check at 814 and `v9x_flip_arm` at 847.
  - With the flip state left idle, GetFlipStatus answers done at once.
  - The refusal at 785 cannot fire.
  - `v9x_flip_wait_done` returns NONE, so the Intel and ViRGE draw waits
    vanish without being touched.

  A counter, `flip_vsync_forced_off`, counts flips the setting changed.
  That is an append to `V9X_D3D_DIAGNOSTICS`, which moves the ABI stamp,
  and `V9XTRACE` dumps it as `FlipVSyncForcedOff`.
- **Status.** The 16-bit driver writes the resolved value to `V9XHW.INI`.
  The page and `V9XSET.EXE` show what the driver did, not what was asked.
- **Settings page.** A checkbox, not a combo, so it fits without a row:
  "Sync flips to refresh". It sits on the `Mode switching:` row at y=150,
  whose value narrows from 166 to about 58 units, the way `PCI ID:` shares
  its row with `Video memory:`. Checked means the application decides,
  which is the default. Unchecked writes `VSync=0`. Checking it again
  deletes the key, as Automatic does for `HighColor`.

  Apply says the change takes effect when the next DirectDraw or Direct3D
  program starts. Phase 0 must confirm that before the text claims it;
  otherwise it says "restart", as `Direct3D=` does. Re-check the dialog at
  640x480.
- **check-tree.** `scripts/check-tree.ps1:902-940` asserts that the SYSTEM.INI
  readers and writers agree. `dd16.c` and `settings_propsheet.c` are
  already in that list; add `VSync` to what it checks. The new files go in
  the file list and in `scripts/lib/host-sources.ps1`.

## Decision 1 (Michael): what "off" covers

- **A. Flips only (recommended).** Flip, GetFlipStatus and the draw waits
  stop waiting for the retrace. WaitForVerticalBlank stays honest, because
  it is the application asking where the beam is. Some games use it to
  pace or to avoid tearing a Blt. Making it return at once changes their
  timing in ways that nobody has measured.
- **B. Flips and WaitForVerticalBlank.** BLOCKBEGIN and BLOCKEND return at
  once, and I_TESTVB still reports the truth. This is closer to what
  2000s vendor panels did, and it frees games that present by Blt after a
  vblank wait. The cost is a game that spins at full speed or misbehaves.

A first, and B as a second value later, only if a game shows the need.

## Decision 2 (Michael): two values or three

- **Two (recommended):** application decides, or off.
- **Three:** add "always on", which ignores an application's NOVSYNC.
  Because NOVSYNC is not advertised, the driver probably already behaves
  that way. The third value would add a page control and a test for
  little.

## Decision 3 (Michael): Intel's ring flip

On the netbook, a flip goes into the ring as MI_DISPLAY_FLIP, and the
display side applies it at the retrace whatever the HAL says. With the
pending state left idle, a second MI_DISPLAY_FLIP could be queued before
the first is taken. That behaviour has not been measured and is not
documented in anything checked.

- **A. Leave the ring flip in place (recommended to start).** GetFlipStatus
  stops waiting, and the HAL still refuses a second ring flip while one is
  pending (keep the 785 check for `WAIT_HW` alone). The application is
  freed from the frame where it can be, and the picture still does not
  tear. On Intel the setting then means "never hold the application", not
  "tear".
- **B. Off means the register write on Intel.** The base is written at
  once, as intel65 did, and the lower-half tearing it showed comes back.
  That is true vsync off, but the MMIO path's latch model was never read
  from a register (`i9xx_scanout.c:1425-1474`).

A, then B only if A is measured and found to throttle anyway.

The same question applies to an application's own NOVSYNC on Intel today.
That case is unmeasured too.

## Phases

0. **Readouts, no code change.**
   - Confirm that `v9x_dd_refresh_framebuffer` runs at each DirectDraw
     program start on A8U4I5. Confirm that a SYSTEM.INI value written by
     the Win32 page is visible to the 16-bit `GetPrivateProfileInt` without
     a restart (one Win16 profile cache is expected; check it).
   - Check the trace counters for whether any DirectDraw application has
     ever reached the HAL with `DDFLIP_NOVSYNC`.
   - Choose the test program: a double-buffered flip loop that reports
     presents per second. If no V9XDDP scene does this, add one. No
     benchmark scores (standing rule).
1. **Policy, host-only, test first.** Write `test_vsync.c` with resolve
   cases (missing, 0, 1, junk) and flip cases (application NOVSYNC × policy).
   Watch it fail, then write `vsync.c`. Gates: check-tree, build-host.
2. **Driver and HAL.**
   - Stamp the bit in both places, apply the policy in `v9x_flip_body`,
     add the counter, bump the ABI, update the trace dump.
   - Make the Intel ring-flip choice from decision 3.
   - Write the status line in `V9XHW.INI`.
   - Gates: check-tree, build-host, run-checks.
3. **Settings page.** Add the checkbox and its read and write, the Apply
   text, and the status in `V9XSET.EXE`. Check the screen at 640x480 on a
   guest.
4. **Machines.** One machine per presentation path, because the setting
   is shared code and applies to all chips (same-design rule).

   | Machine | Path |
   |---------|------|
   | A8U4I5, Rage XL | Mach64 window write |
   | Netbook | Intel ring flip |
   | Hellbender 86Box guest | ViRGE/Trio VGA latch |
   | One no-flip family (Matrox or VBE) | Must show no change and no error |

   For each, record:
   - `FlipVSyncForcedOff`, `FlipStillDrawing`, `FlipWindowClosed` and
     presents per second, with the setting on and off;
   - an operator's look at the screen (tearing expected when off, except
     on Intel under decision 3 A);
   - that turning it back on restores today's counters.

   Stop if any machine hangs or the counters show a flip claimed but never
   presented.
5. **Record.** Write a decision doc with the measurements, and update this
   plan's status and `README.md`.

## Traps already paid for

- A policy bit stamped in only one of the two `engine_caps` writers is
  erased at the next DirectDraw session (2026-09-10).
- Every wait must stay bounded. The setting removes waits; it must not
  add one (intel57, intel63).
- `v9x_flip_untracked` declines flips after a broken retrace source. The
  setting must not clear it or bypass it.
- The page height (`settings_propsheet.rc` header).

## Not in scope

- OpenGL "vsync on" (`wglSwapIntervalEXT`). The ICD has no vsync to turn
  off. Whether it should gain one is a separate plan.
- Advertising `DDCAPS2_FLIPNOVSYNC`. It is worth its own check, but the
  forced path does not depend on it.
- Triple buffering or flip queues beyond what DirectDraw already does.
- Refresh-rate selection.
