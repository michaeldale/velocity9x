# HD 4600: the intel-gma package installs through Have Disk, every file as built

Date: 2026-10-18. Machine: the i5-4570 (`8086:0412`), boots 28-29. The
package is `build\win98se-intel-gma` from this commit's tree; its
INSTALL.TXT, manifest description and HAL description are updated for
the HD 4600. Evidence:
[`../probe/hsw-4570-package-install-2026-10-18/`](../probe/hsw-4570-package-install-2026-10-18/).

Until now every driver change on this machine arrived by WININIT rename.
This is the install a user makes.

## The install

On boot 28, after a `reset`, the package directory was copied to
`C:\V9XPKG`. Then Device Manager, all of it driven through the agent by
mouse and keyboard, with a screenshot at each step:
1. Display adapters, then the HD 4600's Properties, then Driver, then
   Update Driver.
2. "Display a list of all the drivers", then Have Disk, then
   `C:\V9XPKG`.
3. The wizard offered "Velocity9x Intel HD Graphics 4600 (Haswell)" alone
   (`in-9.png`), from `C:\V9XPKG\VELOCI~1.INF`.
4. Next copied the files, and Finish asked to restart.

Before the restart, the install had queued three renames in `WININIT.INI`:
- `glide2x.dll`, `glide3x.dll` and `v9xdisp.drv`, which were in use;
- no `NUL=` lines.

Windows' own restart took it to boot 29.

## Measured on boot 29

- **Files:** no rename was left pending. All ten files the INF copies
  match the package by SHA-256: `V9XDISP.DRV`, `V9XMINI.VXD`,
  `V9XSETP.DLL`, `V9XHAL.DLL`, `V9XGL.DLL`, `GLIDE2X.DLL`, `GLIDE3X.DLL`,
  `ATI3DCIF.DLL`, `V9XTRACE.EXE` and `V9XUPD.EXE`
  (`installed-sha256.txt`).
- **Mode:** the desktop came up at 640x480, the INF's first-boot default.
  `V9XMSW /set:640x480x16` then switched it to High Color.
- **Display Properties:**
  - the Velocity9x and Velocity9x Advanced tabs are both present;
  - the adapter reads "Velocity9x Intel HD Graphics 4600 (Haswell)";
  - Colors read High Color (16 bit) (`in-desk.png`).
- **V9XGLP**, on the installed ICD:
  - `Result=PASS`, 30 Ok keys at 1;
  - `Renderer=Velocity9x HD 4600`, `GL_SGIS_multitexture`;
  - identical to boot 26 apart from free video memory.
- **V9XR3DP:** as before: `Result=COMPLETE`, with the one known Ok=0 (a
  CPU texture, refused).
- **V9XGLIDP:** `Result=PASS failures=0`.
  - The DLL beside it in `C:\V9XGMA` is a copy from boot 26, same source
    and ABI, not SYSTEM's installed copy. SYSTEM's matches the package.
  - The first read of the INI showed `INCOMPLETE` above 21 PASS lines.
    A second read showed the final line, so the first copy was taken
    before the probe's last profile write reached the disk.

## Not covered

- **A fresh Windows:** this was an update over an installed Velocity9x
  on the same device. An install over Windows' own VGA driver has not
  been run on this machine.
- **The starting mode list:** the INF gives the HD 4600 the GMA 950's
  starting mode list, 1024x576 included. The VBE merge replaces it after
  the first boot. Left as it is.
- **The settings page's RunOnce registration** was not read from the
  registry; the tabs' presence is the evidence.
