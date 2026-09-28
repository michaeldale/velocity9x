# ATI Rage Mobility-M texture cache visibility evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop, boot 11.

This is the Phase 4 "repeated texture mutation proves cache visibility"
gate. `ATI4TM.EXE` drives VxD DIOC 30. That is the item 10 texture scene
(DIOC 27's body, unchanged) plus a caller-chosen `TEX_CNTL`: either the
proven `0x40860000`, which includes `TEX_CACHE_FLUSH` (`0x00800000`), or
`0x40060000` without it. No other word is accepted.

Every draw is an RGB565 replace of a uniform 8x8 texture at the same
address `0x204000`, with identical state. Before each draw the CPU rewrites
the texel, alternating red `0xF800` and green `0x07E0`. Sixteen draws are
made with the flush, then sixteen without it.

| Sequence | Draws that sampled the texel just written | Draws that sampled a stale texel |
|---|---:|---:|
| With `TEX_CACHE_FLUSH` | 16 of 16 | 0 |
| Without | 8 of 16 (the green ones only) | 8 of 16 |

Without the flush, all sixteen draws returned green. Green was the last
texel loaded under a flush, at the end of the first sequence. The cache
kept it indefinitely across CPU rewrites, backup/restore of the page, and
full idle waits. The report's `NoFlushStaleDraws=7` excludes draw 0 by
construction; draw 0 was stale too.

Conclusion: `TEX_CACHE_FLUSH` is both necessary and sufficient for a CPU
texture upload to be visible to the next draw, and this test detects its
absence. The engine must set it on the first draw after any texture
upload or rebind. Two runs produced byte-identical reports, and every draw
in both had zero exterior, texture/guard and restoration mismatches, 256
changed pixels, and no timeout or reset. Before this run, the item 9 to 12
tables were rerun against the same VxD and reproduced their pixel dumps.

| File | CRC32 | SHA-256 |
|---|---|---|
| `ATI4TM.TXT`, `ATI4TM-PASS2.TXT` | `28FE652F` | `39111A4242D08EEB0376FA7F1D08D343A5F15F076A0029BBB327914A25686ED2` |

The texture page is restored after every draw. The no-flush sequence
leaves the texture cache holding green; the stock driver does not use the
3D engine, and no Velocity9x path runs on this board yet.
