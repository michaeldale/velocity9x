# ATI Rage Mobility-M Phase 4 fog evidence

Physical target: Gateway Solo 2150, ATI Rage Mobility-M `1002:4C4D`
revision `64`, 4 MiB SGRAM, 1024x768x16 desktop, boot 11.

Phase 4 item 12: fog with blending disabled. Following Mesa
`mach64_state.c` and its Utah-GLX notes, fog is `SCALE_3D_CNTL`
`ALPHA_FOG_EN=2` (`0x1000`) with the SRCALPHA/INVSRCALPHA blend fields. In
that mode the destination operand becomes `DP_FOG_CLR` (`0x6C4`) and the
factor is the vertex specular alpha. That is D3D's vertex-fog convention:
255 means no fog.

VxD DIOC 29 draws the Phase 3 flat triangle and accepts exactly two
`SCALE_3D_CNTL` words: fog off `0x000100C1` and fog on `0x002C10C1`. Fog
combined with blending, or any other word, fails before MMIO is mapped.
`DP_FOG_CLR` is state slot 11, which every scene already writes. The
19-write setup packet carries no specular colour, so the three
`VERTEX_n_SPEC_ARGB` words (`0x24C`, `0x26C`, `0x28C`) go out in one
reserved batch after state and before the vertex batches. Vertex registers
only latch until the `ONE_OVER_AREA` trigger, so this is equivalent to
interleaving them. Those three writes are outside the VxD's 38-entry
transcript, so the publisher records their value for each scene.

The vertex colour was `0x30B04C28`. Its alpha, `0x30`, differs from every
specular alpha used.

| Scene | Fog | `DP_FOG_CLR` | Specular alpha | Observed | Predicted |
|---|---|---|---:|---|---|
| 0 | off | `0x0020D0E0` | 0 | `0xB265` | `0xB265` (vertex colour) |
| 1 | on | `0x0020D0E0` | 255 | `0xB265` | `0xB265` |
| 2 | on | `0x0020D0E0` | 0 | `0x269C` | `0x269C` (fog colour) |
| 3 | on | `0x0020D0E0` | 128 | `0x6C70` | `0x6C70` |
| 4 | on | `0x0020D0E0` | 64 | `0x4576` | `0x4576` |
| 5 | on | `0x0020D0E0` | 192 | `0x8B6A` | `0x8B6A` |
| 6 | on | `0x00F01030` | 128 | `0xC965` | `0xC965` |

Build `ati-phase4-fog-20260928-a` passed twice byte-identically. All seven
scenes matched `C = (Cv*f + Cfog*(255-f)) / 255`, truncated, with no
tolerance needed. Every scene had status `0x0001FFFF`, uniform probes,
zero exterior, guard and restoration mismatches, 256 changed pixels in
`(8,6)` through `(38,21)`, and no timeout or reset. The rejected
alternatives miss by 26 units (factor from vertex alpha), 33 units
(inverted factor) and 33 units (fog ignored). `DP_FOG_CLR` is ARGB8888, as
Mesa packs it at `cpp=4`, even on this 16-bpp target.

Rounding: this data also narrows item 9. Of the four blend rules consistent
with all 36 factor pairs, only factor/255 with truncation predicts all
seven fog pixels. The /256 rules predict five and (a+1)/256 predicts six.
That is conclusive for blending only if fog shares the blend unit's
arithmetic. The register usage implies it does, but this was not measured
separately.

Before this run, the item 9, 10 and 11 tables were rerun against the same
VxD and reproduced their pixel dumps (`9076CD43`, `6583D565`,
`0FC48B56`).

The specular registers keep the last scene's value after the run. They
were not saved, because their readback has not been validated. No
production path depends on them yet.

| File | CRC32 | SHA-256 |
|---|---|---|
| `ATI4FG.TXT`, `ATI4FG-PASS2.TXT` | `D18F4F3A` | `B3BA936DFC72DCE16F608AD6035C59D6A3DA4608C9B11E7876FD7931F6583F9C` |
| `ATI4FG.BIN`, `ATI4FG-PASS2.BIN` | `593D8EE6` | `960D0F8E6CCB8542E14A63EB78B677AF5BD808F2DBD54CBBA437B179A32D382E` |

Textured fog is not covered here. Utah-GLX notes that texture alpha is
unavailable while fogging, and the engine must clear `TEX_MAP_AEN` for a
fogged textured draw. That combination needs its own scene before it is
advertised.
