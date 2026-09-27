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
