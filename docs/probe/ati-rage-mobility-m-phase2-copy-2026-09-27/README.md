# ATI Rage Mobility-M Phase 2 overlapping screen copy

Build `ati-p2-copy-20260927-a` ran one overlapping 16x8 RGB565 screen copy in
each of the four X/Y direction combinations on the Gateway Solo 2150. Each
case used a 64x32 surface at VRAM offset `00200000`, a 128-byte pitch, unique
per-pixel input, a CPU memmove reference, and a comparison of the complete
4 KiB page. The report passed with zero mismatches in every direction and an
exact VRAM restore (`CRC32 8C8B089D`, SHA-256
`2593ED5CC01B9A38E23D3267BEE768F776B54AA58ADCB836AA75C06FD33767FE`).

Build `ati-p2-copy-20260927-b` then cycled the same four cases 250 times for
1,000 total copies. Every copy used the measured 16-bpp source-copy state and
the mandatory VTB+ post-copy full-idle wait before CPU access or the next
operation. All four accumulated mismatch counts and the restoration mismatch
count remained zero. The guest completed in 689 ms and the agent remained
reachable on the same boot (`CRC32 8A544F11`, SHA-256
`3A7A11767F20F37C9386734809D933D5499180D7C88342A3C88304E5325F8F76`).

The diagnostic backed up and restored the target page, ten non-trigger engine
registers, and `MEM_BUF_CNTL`. It never replayed a trigger register.

Build `ati-p2-present-20260927-b` retained the 1,000-copy overlap stress and
then copied a 16x2 rectangle from the 128-byte-pitch offscreen surface to the
live 2048-byte-pitch, 1024-pixel scanout. The diagnostic backed up the complete
first two scan lines, verified the 32 presented pixels and all 2,016 untouched
guard pixels after the mandatory full-idle/cache-invalidate boundary, and
restored the entire 4 KiB front-buffer window. Presentation and restoration
mismatch counts were both zero (`CRC32 BAA3520D`, SHA-256
`1D5A98B32AE9B19E4F0E4B3A8FDDD7522EAEB5FA255E6E44ED585A891689E38B`).

An earlier invocation correctly refused while display power management had
disabled PCI memory decode and cleared the live BARs. No MMIO or framebuffer
access occurred on that path; waking the desktop restored the validated
configuration before the successful run.

The presentation path was then changed to consume the live `CRTC_OFF_PITCH`
rather than assuming the native-mode pitch. Live mode switches with the stock
ATI driver produced the expected 1,280-byte pitch at 640x480x16 and 1,600-byte
pitch at 800x600x16. The complete 1,001-copy diagnostic passed at both modes
with zero overlap, presentation, offscreen-restore, or front-restore
mismatches (`ATI2CPY-640.TXT` CRC32 `CF7A4D85`; `ATI2CPY-800.TXT` CRC32
`D826A592`). The desktop was restored to 1024x768x16 afterward.
