# 86Box Mach64 VT2: Have Disk install of the released 0.18.0 ATI package, 2026-10-15

The first of the two checks the 0.18.0 release owed
([STATUS](../../STATUS.md)).

- **Guest:** 86Box `Win98SE-Mach64VT2` (ATI Mach64 VT2, `1002:5654`,
  4 MB), agent rel-0.6.0 on host port 9873, boots 122-123. Before: a
  development build of the `ati` family, still bound to the INF's
  "VBE-generic display" entry, with 3dfx's GLIDE2X 2.56.00.0459 and
  GLIDE3X 3.03.00.0670 in SYSTEM and no ATI3DCIF.DLL. ATI's own
  ATI3DCIF.DLL 4.11.2692 (from A8U4I5's `C:\ATI\SUPPORT\w82474en`) was put
  in SYSTEM first, so the INF's overwrite line had ATI's copy to replace.
- **Package:** `releases/0.18.0/velocity9x-0.18.0-ati.zip`. It matched
  `SHA256SUMS.txt`; all 27 files matched its own `SHA256.TXT`. Unpacked to
  `C:\V9XHD18`.

**Route:** Display Properties, Settings, Advanced, Adapter, Change, Next,
"Display a list of all the drivers", Next, Have Disk, `C:\V9XHD18`, OK.
The INF offered one compatible model, "Velocity9x ATI Mach64 VT2"
(`select-device.png`). Then OK, Next, Finish (`installed.png`), Close,
and Yes at the restart prompt (`restart-prompt.png`).

## Results

- **No prompt** about replacing a newer file, for ATI3DCIF.DLL or anything
  else.
- **Before the restart:**
  - ATI3DCIF.DLL, V9XGL.DLL and V9XMINI.VXD were already replaced.
  - The three files in use were staged in `WININIT-staged.INI`:
    `v9xdisp.drv`, `v9xhal.dll` and `v9xsetp.dll`.
- **After the restart (boot 123)**, against the package by SHA-256:

  | File | Result |
  |---|---|
  | V9XDISP.DRV, V9XMINI.VXD, V9XHAL.DLL, V9XSETP.DLL, V9XGL.DLL, ATI3DCIF.DLL | equal to the release |
  | GLIDE2X.DLL, GLIDE3X.DLL | 3dfx's 2.56 / 3.03 kept, by design (the INF keeps a higher-versioned Glide: [record](../../decisions/2026-10-09-glide-packaging-copy-flag.md)) |

- WININIT.INI was consumed. `V9XBOOT.INI`: `Stage=enable-ok`,
  `DriverInitResult=ok`. Desktop at 1024x768.

The guest is left on the released 0.18.0. It is the machine for the second
owed check: a first update through Check for updates once 0.18.1 is
published.
