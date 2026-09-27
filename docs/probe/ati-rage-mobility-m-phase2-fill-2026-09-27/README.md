# ATI Rage Mobility-M Phase 2 guarded solid fill

Build `ati-p2-20260927-a` ran twice on the Gateway Solo 2150. Both reports
were byte-identical with CRC32 `B11C537F`.

The diagnostic used a 4 KiB page at VRAM offset `00200000`, above the
1024x768x16 visible surface. It backed up the page, seeded every pixel with
`A55A`, and submitted one 16x16 RGB565 `F81F` fill at `(8,8)` with a 64-pixel,
128-byte pitch. It then waited for idle, set the Mobility read-cache
invalidation bit, checked every target and guard pixel, restored the ten
non-trigger engine registers, restored `MEM_BUF_CNTL`, and restored the exact
4 KiB backup.

Both runs reported zero interior, guard, and restoration mismatches. The
display remained stable and the agent remained reachable.

Build `ati-p2-20260927-b` then repeated the same bounded transaction with
1,000 fills alternating RGB565 magenta and green. It reserved the exact two
FIFO slots before each colour/trigger pair and drained once after the final
fill. The final green rectangle, all guards, engine-state restoration, and the
4 KiB VRAM restoration passed with zero mismatches. Its report CRC32 was
`5954CD4E`.
