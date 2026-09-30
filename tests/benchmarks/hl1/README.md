# Half-Life 1 benchmark

Use `mwd5.dem` for future HL1 performance comparisons, as requested by the
user on 2026-09-30. It replaces `v9xbench` as the standard future benchmark;
existing recorded results retain their original demo and methodology.

1. Patch Half-Life to version **1.1.1.0**.
2. Set the desktop shortcut target to
   `"C:\SIERRA\Half-Life\hl.exe" -console`.
3. Copy [mwd5.dem](mwd5.dem) into `C:\SIERRA\Half-Life\valve\`.
   The supplied creation notes said `value`; the base game's folder is `valve`.
4. Open Half-Life, select Console, and enter `timedemo mwd5`.
5. Run it **three times** and report the **best FPS of the three**. The first
   run is usually slower; runs two and three should be very similar.

Retain all three console results, including frame counts, elapsed time and
FPS, so repeat consistency can be checked. Use the same game version, demo,
renderer, resolution and settings for each compared driver. Record those
settings and the driver identity with the results. If runs two and three
differ substantially, investigate before treating the comparison as stable.

The demo was copied from `C:\everything\mwd5.dem` without changing its bytes.
The original file remains in place. See [SHA256.txt](SHA256.txt) for its hash.
