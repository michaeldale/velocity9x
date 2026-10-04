# A VSync setting on the Velocity9x page, measured on the Rage XL

Date: 2026-10-04. Machine: A8U4I5, ATI 3D Rage XL PCI (`1002:4752`),
Mach64 engine path, 1024x768x16 at 60 Hz. Plan:
`docs/plans/vsync-off-setting.md`. Evidence:
`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/vsync/`.

## Decisions taken

- **Three values: Game decides, Always on, Always off** (Michael, decision
  2). SYSTEM.INI `[Velocity9x] VSync=` holds 0, 1 or 2; the page deletes
  the key for Game decides. Zero is Game decides so that an absent key or
  text `GetPrivateProfileInt` cannot parse never turns vsync off.
- **Flips only** (decision 1, the plan's recommendation; not answered).
  WaitForVerticalBlank is unchanged.
- **Intel keeps the ring flip** (decision 3, the plan's recommendation; not
  answered). Superseded later the same day by "B", below, once the
  netbook showed this changes nothing. An unsynced flip on a scanout that queues flips itself stays
  tracked: Flip still refuses to queue a second one behind it, while
  GetFlipStatus and the draw waits stop holding the application. This also
  changes an application's own `DDFLIP_NOVSYNC` on Intel, which used to
  leave the flip untracked and could queue a second MI_DISPLAY_FLIP behind
  a pending one. Not measured on the netbook.

## What was built

- `src/common/vsync.c`: the resolve and the per-flip rule, host-tested
  in `tests/host/test_vsync.c`. The test was written first and failed ten
  checks against a stub.
- `dd16.c` reads the key every time DirectDraw creates its driver object.
  It stamps `V9X_DD_ENGINE_CAP_VSYNC_ON` or `_OFF` in both places that
  build `engine_caps`, and writes the resolved state to `V9XHW.INI` as
  `VSync=`.
- `v9x_flip_body` applies the rule at both blank gates. A new counter,
  `flip_vsync_overridden` (`FlipVSyncOverridden`, ABI 2026100308), counts
  accepted flips whose vsync the setting changed.
- The page has a VSync selector on the Active mode row, so the dialog is
  no taller.

## Measured

V9XDDP's default run times 20 back-to-back `Flip(DDFLIP_WAIT)` calls.
All three runs were in one boot, with the setting changed on the page
between them and no restart. The counters are cumulative across the
boot.

| Setting | Flip20Ms | FlipMaxMs | FlipHandled | FlipVSyncOverridden | FlipStillDrawing | FlipWindowClosed |
|---------|---------:|----------:|------------:|--------------------:|-----------------:|-----------------:|
| Game decides (no key) | 332 | 18 | 23 | 0 | 7,349 | 92,223 |
| Always off | 0 | 0 | 46 | 23 | 7,349 | 92,223 |
| Always on | 332 | 18 | 69 | 23 | 14,719 | 180,776 |

- With vsync on, 332 ms for 20 flips is 16.6 ms each: one 60 Hz frame
  per flip.
- With Always off, every flip was overridden (+23), and neither wait
  counter moved. The flip loop no longer waited for the blank.
- Always on behaved like Game decides. V9XDDP never passes
  `DDFLIP_NOVSYNC`, so there was nothing to override. That Always on
  overrides an application's NOVSYNC is covered only by the host test.
- The change took effect at the next DirectDraw program with no restart.
  That is what the page's Apply message says, and this run is the
  evidence for it.
- On reopening, the page showed the saved value each time.
  `C:\WINDOWS\SYSTEM.INI` on disk still read `VSync=1` after the page
  had gone back to Game decides. The Win16 profile cache had not been
  written out yet, and both the page and the driver read through that
  cache. The existing Direct3D= and HighColor= settings work the same
  way.

`FlipPixelOk=0` in every run is the probe's GDI read-back, which sees
only the GDI page once real flips are in play (the probe's own comment
at `/hold`). It is unrelated to this change.

## The netbook: the setting reaches the ring flip and changes nothing

Same day, build `795ea49`, on the HP Mini 110 (945GSE), 1024x576x16,
over wifi at 10.0.1.254. The DRV, VxD, HAL, page and ICD were deployed
together. Evidence: `docs/probe/netbook-vsync-and-textures-2026-10-04/`.

This netbook's Display Properties has no Velocity9x tab: its page
handler was never registered. So the setting was changed by editing
SYSTEM.INI. The original was fetched, the `[Velocity9x]` section with
`VSync=` was appended, the file was pushed, and the original was put
back afterwards. The driver read each edit with no restart.

| Setting | Flip20Ms | FlipHandled | FlipVSyncOverridden | FlipStillDrawing | FlipRingIssued |
|---------|---------:|------------:|--------------------:|-----------------:|---------------:|
| Game decides (no key) | 321 | 23 | 0 | 89,529 | 25 |
| Always off | 324 | 46 | 23 | 187,590 | 50 |
| Always on | 317 | 69 | 23 | 291,871 | 75 |

All 23 flips were overridden under Always off, and every one went
through the ring. The loop still ran at one frame per flip, and Flip
refused about 98,000 times per run either way. This is decision 3's
"A" measured. The flip stays tracked while MI_DISPLAY_FLIP is pending,
so Flip will not queue the next one. GetFlipStatus and the draw waits
are released, but a flip-bound application is not. On Intel, Always
off therefore changes nothing a game would notice. The plan's next step
for this outcome is decision 3 "B": the register write instead of the
ring when vsync is off.

## Decision 3 "B": an unsynced flip writes the register

Michael chose "B" the same day. An unsynced flip now goes through
`v9x_set_display_start_now`. On Intel that is the plane-base register
write that intel65 measured tearing in the lower half, with no
MI_DISPLAY_FLIP. On the Mach64 and VGA scanouts it is the same write as
before, because their blank wait is in the core and an unsynced flip
already skips it. Nothing is queued, so `v9x_flip_arm` leaves an unsynced
flip idle on every scanout, and the morning's "tracked but not waited
for" state is gone. An application's own `DDFLIP_NOVSYNC` on Intel also
takes this path, and so never queues a ring flip behind another.

The HAL alone was redeployed (ABI unchanged). The setting was changed on
the netbook's page, which works since its handler was registered
(`docs/issues/2026-10-04-netbook-settings-page-not-registered.md`).
Each row below is a fresh boot counter:

| Setting | Flip20Ms | FlipMaxMs | FlipVSyncOverridden | FlipStillDrawing | FlipRingIssued |
|---------|---------:|----------:|--------------------:|-----------------:|---------------:|
| Always off, register write | 4 | 2 | 23 | 0 | 2 |
| Game decides, after it | 331 | 21 | 23 (unchanged) | 99,288 | 27 (+25) |

Under Always off, Flip never refused and no ring flip was issued for
the probe's flips. The 2 counted come from other callers;
FlipToGDISurface also calls the ringed write. Game decides went back to
the ring and one frame per flip.

Not watched: whether the panel tears, or shows anything wrong, with the
register write. intel86 found the panel flickering with a buffer under
construction on this part whatever the write timing was. Under vsync
off, that is expected rather than a defect, but nobody has looked.

## Not established

- Tearing was not watched on the monitor. The counters say the waits
  were skipped; nobody looked at the screen.
- The ViRGE/Trio VGA latch and a no-flip family (Matrox, VBE) were not
  run. The setting is shared code, so it applies to them as built.
- Games other than V9XDDP were not run with the setting.
