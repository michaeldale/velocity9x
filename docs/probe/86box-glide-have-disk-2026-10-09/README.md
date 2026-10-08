# 86Box Win98 SE Trio64: Have Disk install with GLIDE2X.DLL, 2026-10-09

- Guest: 86Box `Win98SE-Trio64` (S3 Trio64, `5333:8811`), agent 0.5.2 at
  127.0.0.1:9871, boots 359-361. Before: an old Velocity9x (no
  `V9XGL.DLL`) and a 3dfx `GLIDE2X.DLL` 2.56.00.0459 in SYSTEM, inherited
  from the profile it was cloned from.
- Package: `build\win98se-s3`, 0.14.0, build `b0e80ec-dirty`: built from
  the working tree then committed as 8cef5b6. Copied to `C:\V9XHD`.
- Record: [Glide packaging](../../decisions/2026-10-09-glide-packaging-copy-flag.md).

Route both times: Display Properties, Settings, Advanced, Adapter,
Change, "Display a list of all the drivers", Have Disk, `C:\V9XHD`,
"Velocity9x S3 Trio32/64 86C764", Next, Finish, Close, restart Yes.

| Install | SYSTEM\GLIDE2X.DLL before | During | After the restart |
|---|---|---|---|
| 1 (boot 359 to 360) | 3dfx 2.56 | No version prompt; nothing staged for Glide (`B360-WININIT.INI` stages only `v9xdisp.drv` and `v9xsetp.dll`); `V9XGL.DLL`, `V9XHAL.DLL`, `V9XMINI.VXD` copied at once, sizes equal to the package | 3dfx 2.56, same hash; `V9XDISP.DRV` equal to the package; `DriverInitResult=ok` |
| 2 (boot 360 to 361) | Velocity9x 0.13.0, put there by hand | No version prompt; `B361-WININIT.INI` stages `glide2x.001` over it, and `.001` is the package's DLL by hash | The package's 0.14.0 DLL, same hash; `DriverInitResult=ok` (`B361-V9XBOOT.INI`) |

The 3dfx 2.56 was then put back and verified by hash. The guest is left
on the 0.14.0 package. `C:\V9XHD` remains on the guest.
