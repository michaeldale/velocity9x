# Turning Direct3D off works from a settings-page control, and only a restart hides the HAL device

Date: 2026-08-30
Branch: `main`
Plan: [`s3-trio64-voodoo2-hybrid-3d.md`](../plans/s3-trio64-voodoo2-hybrid-3d.md), mode 1

Guest: `Win86SE`, 86Box, `s3` family, ViRGE/DX 86C375, 4 MiB, 1024x768x16,
agent port 9869. Driver `2dc3e9d-dirty`, installed over the previous build by
`WININIT.INI` rename (`V9XDISP.DRV`, `V9XMINI.VXD`, `V9XHAL.DLL`,
`V9XSETP.DLL`).
Gate: `V9XDDP.EXE`, result deleted before every run, `Build=` checked against
the build under test on every run, and the `exec` exit code checked on the two
runs it could be.

## What was being tested

`[Velocity9x] Direct3D` in `SYSTEM.INI`, read at Enable, resolved by
`src\common\d3dmode.c` against the chip's engine descriptor, and applied by
`dd16.c` clearing `V9X_DD_ENGINE_CAP_D3D` before the descriptor is stamped into
the DirectDraw shared block. No 32-bit code and no shared-block layout change.

## The runs

Nine runs across seven boots. Every row is the first `V9XDDP.EXE` run of its
boot except A and E, which are noted. `Direct3DMode=` is the driver's own
report, from `V9XHW.INI`. G and H are the two the settings page drove; the
section below covers what it did.

| | Boot | Key | `Direct3DMode` | `D3DHalFound` | `D3DDeviceCount` | `TexFormatCount` |
|---|---|---|---|---|---|---|
| Baseline | 499 | absent | `hardware` | 1 | 4 | 2 |
| A | 499, 2nd run | `=1`, written mid-session | `user-disabled` | **1** | **4** | 0 |
| B | 500 | `=1` from boot | `user-disabled` | **0** | **3** | 0 |
| C | 501 | `=2` from boot | `mode-unimplemented` | 0 | 3 | 0 |
| D | 502 | `=0` from boot | `hardware` | 1 | 4 | 2 |
| E | 502, 2nd run | `=0` | `hardware` | 1 | 4 | 2 |
| F | 503 | `=0` from boot | `hardware` | 1 | 4 | 2 |
| G | 504 | `=1`, written by the page | `user-disabled` | 0 | 3 | 0 |
| H | 505 | `=0`, written by the page | `hardware` | 1 | 4 | 2 |

`Stage=enable-ok` and `Surface=pitch=2048 bpp=16 w=1024 h=768` on every boot.
DirectDraw stayed fully working throughout: `BltFillPixelOk=1`,
`SrcCopyPixelOk=1`, all four overlap cases with `...Seen=64`,
`RestoreHr=0x00000000`. `FlipPixelOk=0` on every run, which is the known
pre-existing result and not this change.

### Baseline and restore are byte-identical

The baseline run - new driver, no key - reproduced the recorded ViRGE ladder
exactly: `D3DTrianglePixelRaw=31744`, `D3DBaseTextureRaw=992`,
`D3DMipmapLevelRaw=31`, `D3DTrilinearRaw=495`, `Tex4444Raw=992`,
`TexFormatCount=2`, `D3DZCompareOk=1`, `D3DZWriteMaskOk=1`.

Run D, after the key had been set to 1, then 2, then back to 0 across three
reboots, differs from that baseline in **no `D3D*` or `Tex*` key at all** - not
even the texture handles. The setting is reversible and leaves nothing behind.

### Run B is the shape mode 1 was aiming at

`D3DDeviceCount=3`, and the three are `Ramp Emulation`, `RGB Emulation` and
`MMX Emulation`. No `Direct3D HAL` entry is enumerated. That is the same shape
already recorded for a family with no D3D engine on `Win98SE-Mach64VT2`, and it
is what lets an application fall back cleanly rather than fail.

## The finding that changes what the settings page may say

**A re-enable stops the driver serving Direct3D. It does not stop DDRAW
offering the device.**

Run A wrote the key into `SYSTEM.INI` mid-session and did not reboot. Something
in the probe's own exclusive-mode path re-entered Enable - not deliberately
triggered - and the driver side took effect: `Direct3DMode=user-disabled` was
republished and `TexFormatCount` went 2 to 0. But DDRAW still enumerated four
devices with a `Direct3D HAL` among them, and every attempt to use it failed:

```
D3DHalFound=1        D3DDeviceCount=4      TexFormatCount=0
D3DCreateDeviceHr=0x80004002   (E_NOINTERFACE)
D3DMainCapsHr=0x88760231       D3DMainIsHardware=0
D3DV1DeviceHr=0x80004002       D3DV1DeviceOk=0
```

So mid-session the machine is in the one state mode 1 exists to avoid: an
application enumerates a hardware Direct3D device, selects it, and fails at
`CreateDevice` instead of falling back to `RGB Emulation`. Only the fresh boot
in run B removes the entry.

**Therefore the settings page says "takes effect after restart", and that is
now measured rather than assumed.** It is also the answer to the plan's step 1,
which had been listed as a prerequisite to mode 1 and was instead answered by
it.

Run A is weaker evidence than the others because it was the second probe of its
boot - see below - but the keys it turns on are not the flaky ones, and the
structural reading is straightforward: DDRAW built its device list from the
`DDHALINFO` of the session before the key was read.

## A procedural finding: the probe is a first-run-after-boot instrument

Run E was a second `V9XDDP.EXE` in the same boot as run D. It reported
`ExitCode=0` and `Result=COMPLETE`, and three functional keys regressed against
a run that had just passed:

```
D3DZWriteMaskOk        1 -> 0   (D3DZNoWriteRaw 992 -> 32767)
D3DDepthFogOk          1 -> 0
D3DVertexAlphaBlendOk  1 -> 0
```

Run F then rebooted and ran the probe non-detached as the first run of boot
503: `ExitCode=0`, and every functional key matched the baseline. So the
variable is **the second run in a boot, not the `-Detach` flag**, and the three
keys are the probe's own state carry-over rather than anything in the driver.

This is worth stating because the existing hygiene rules - delete the result,
check the exit code, check `Build=` - do not catch it. A second run passes all
three and still reports three false failures. **Add a fourth rule: one probe
run per boot.**

## The settings page, driven the same day

The Direct3D row became a combo box - replacing the value label in place,
because the page's height budget has no room for a new row and a dropped-down
combo costs nothing vertically. Driven through the agent on the guest at both
1024x768x16 and 640x480x16:

- The list holds **two entries**, `Hardware (the chip's own engine)` and
  `Disabled - advertise no Direct3D`. Software, hybrid and offload are not
  offered, on the same rule the driver applies to capabilities: a control
  promising a rendering path this build does not have is the UI form of
  advertising an unimplemented capability.
- Opening the page selects what `SYSTEM.INI` holds, and `Apply` stays greyed
  until the selection differs from it, so `OK` on an untouched page writes
  nothing.
- `Disabled` then `Apply` wrote `Direct3D=1` and showed "It takes effect after
  you restart Windows. Until then DirectDraw applications continue to see the
  previous setting." The reboot gave `D3DHalFound=0`, `D3DDeviceCount=3`,
  `TexFormatCount=0`.
- `Hardware` then `Apply` wrote `Direct3D=0`. The reboot gave
  `D3DHalFound=1`, `D3DDeviceCount=4`, `TexFormatCount=2`, and **no functional
  key differing from the baseline**.

**At 640x480 the page still fits**, `OK`/`Cancel`/`Apply` on screen, which is
the check `settings_propsheet.rc` demands of anything touching the layout. The
dialog is 211 dialog units before and after.

### One trap worth the sentence

The first attempt rendered the *old* page. `V9XSETP.DLL` is registered under
its bare name, so `LoadLibrary` searches the current directory before
`SYSTEM` - and the agent's `exec` had the job folder as its working directory,
which held the previous copy pushed with the package. Launch
`RUNDLL32.EXE SHELL32.DLL,Control_RunDLL DESK.CPL` from a directory with no
`V9XSETP.DLL` in it, or refresh the job copy too.

## What is still unmeasured

- **Whether Windows 9x picks a second GPU's Direct3D** once this one stops
  advertising. That was mode 1's stated motivation and this guest has one card.
- **The disabled-control path.** On a chip with no 3D engine the combo is meant
  to show the card's answer and be greyed. That branch is written and the host
  tests cover the state it reads, but no Trio64 or tier-0 guest has opened the
  page.
- **Every other family.** Only the ViRGE was run. The `vbe`, `ati` and
  `matrox-m2` families resolve `none` by construction, which is asserted in the
  host tests and not on hardware.

## Guest left as

Driver `2dc3e9d-dirty` installed, 1024x768x16, `[Velocity9x] Direct3D=0` in
`SYSTEM.INI` - written by the settings page itself, behaviourally the same as
the absent key it replaced, and left in place as a record that the ladder ran.
The package is at `C:\V9XREMOTE\JOBS\d3dmode-001`.
