# ATI Rage Mobility-M Phase 4 scissor evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop, boot 11.

Phase 4 item 11. `ATI4SC.EXE` drives VxD DIOC 28, which draws the Phase 3
flat magenta triangle with caller-supplied `SC_LEFT_RIGHT` and
`SC_TOP_BOTTOM`. Everything else in the scene is unchanged. The VxD refuses,
before mapping MMIO, any rectangle that is inverted or extends past the
64x28 guarded target. A scissor may legitimately exclude the fixed interior
probes, so the VxD skips them for this scene. Instead the publisher
compares all 1,792 pixels of each dump against the scene 0 reference
intersected with the rectangle, and requires the sentinel `0xA55A`
everywhere else.

Rectangles are half-open and encoded as `v9x_m64_build_flat_state` encodes
them: inclusive right `right - 1`, inclusive bottom `bottom - 1`. The split
lines at x=20 and y=12 cross the triangle with pixels on both sides, and
two scenes are a single column and a single row, so an off-by-one on
either inclusive edge would change the image.

| Scene | Rectangle `[l,r) x [t,b)` | Changed pixels |
|---|---|---:|
| Full | `[0,64) x [0,28)` | 256 |
| LeftOfX20 | `[0,20) x [0,28)` | 156 |
| RightFromX20 | `[20,64) x [0,28)` | 100 |
| AboveY12 | `[0,64) x [0,12)` | 156 |
| BelowFromY12 | `[0,64) x [12,28)` | 100 |
| InteriorBox | `[14,30) x [9,17)` | 92 |
| ColumnX20 | `[20,21) x [0,28)` | 10 |
| RowY12 | `[0,64) x [12,13)` | 19 |
| VertexCorner | `[8,12) x [6,10)` | 16 |
| MissesTriangle | `[40,64) x [0,28)` | 0 |

Build `ati-phase4-scissor-20260928-a` passed twice byte-identically. Every
scene had status `0x0001FFFF`, zero pixel mismatches against the
intersected reference, and zero exterior, guard and restoration mismatches,
with no timeout or reset. The complementary splits sum to the reference:
156 + 100 = 256 at both x=20 and y=12. The unscissored reference is
byte-identical to every committed Phase 3 `ATI3D0` capture, including the
cold-boot one. Before this run, the item 9 and item 10 tables were rerun
against the same VxD and reproduced their pixel dumps (`9076CD43`,
`6583D565`).

| File | SHA-256 |
|---|---|
| `ATI4SC.TXT`, `ATI4SC-PASS2.TXT` | `F070463A580A773FDB4565E5F53387452EA3F974E1E5337950A4B801F1F467B8` |
| `ATI4SC.BIN`, `ATI4SC-PASS2.BIN` | `F7409E2A37CA7C71ED8F104ADC6D5D8AE3269156A44298A58C5821123E832B4C` |

The agent's CRC32 values are `5840D4C5` (TXT) and `0FC48B56` (BIN). Each BIN
holds one 64x28 RGB565 dump per scene in table order, top row first.
