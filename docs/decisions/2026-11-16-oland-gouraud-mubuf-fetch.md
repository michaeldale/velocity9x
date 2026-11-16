# Oland: vertices through MUBUF in place of FLAT, and the Gouraud ramp exact

Date: 2026-11-16

Status: **measured, three runs of the draw.** W9QI0DAD (Oland XT
`1002:6610`), boot 49, ati Oland row, desktop 1024x768x16. Step 2 of
[`../plans/ati-oland-gfx6-3d.md`](../plans/ati-oland-gfx6-3d.md), after
the first rectangle
([`2026-11-16-oland-first-rectangle.md`](2026-11-16-oland-first-rectangle.md)).
Evidence:
[`../probe/oland-w9qi0dad-2026-11-16/`](../probe/oland-w9qi0dad-2026-11-16/)
`sicp-gouraud1`, `sicp-fetchdump1`, `sicp-fetchdump-2`, `sicp-gouraud-2`,
`sicp-gouraud-final`, `sicp-rect-final`, `bltx-after-gouraud-16bpp`.

## Why

Every Direct3D draw on Carrizo and Mullins starts in the TL vertex
shader (`vi_shader.c`), which fetches each 32-byte vertex with two FLAT
loads. SI has no FLAT. Its drivers fetch vertices through buffer
resources.

## What changed

- **`v9x_si_shader_tl_vs`** (`src\chipsets\ati\si\si_shader.c`). It keeps
  the GCN 3 program's first instruction (VertexID x 32 into v1) and
  everything from its `s_waitcnt` on, translated by
  `v9x_si_shader_from_vi`. The FLAT fetch between them is replaced by two
  MUBUF `buffer_load_dwordx4` with OFFEN through s[0:3], at offsets 0 and
  16, into the same VGPRs (v3-v6, v9-v12): 41 dwords. The MUBUF words are
  LLVM's SI encoding, op 0Eh. The host test checks them against LLVM's SI
  assembler test (`buffer_load_dwordx4 v[1:4], v0, s[4:7], 0 offen` is
  E0381000h, 80010100h). The function refuses if the GCN 3 program's
  fetch is no longer where it expects.
- **`v9x_vi_3d_tl_draw` in GFX6 mode** gives the VS four user SGPRs, a V#
  over the vertices: base address, stride 0 (OFFEN offsets are bytes),
  NUM_RECORDS the buffer's bytes, DST_SEL XYZW, NUM_FORMAT FLOAT,
  DATA_FORMAT 32 (radeonsi's raw buffer on GFX6). Carrizo and Mullins
  still get two SGPRs, the address.

## Runs

`V9XSICP /gouraud` is V9XCK3D's Gouraud draw. Two triangles of TL
vertices cover the 64x16 target, with red from 0 at the left edge to FFh
at the right, green 80h, blue 40h, alpha FFh, and every pixel must be
within +-2 of the ramp at its centre. The PS is `vi_shader.c`'s Gouraud
PS translated (VINTRP, `v_cvt_pkrtz`, a compressed FP16 export).

1. **First run, FAIL: a probe bug.** One triangle-shaped region was drawn,
   in a single colour, 80000901h, which is dword 4 of the VS itself. The
   probe had taken the vertex data's start in its data array before
   appending the two shaders, so the vertex buffer was filled with shader
   code, and the VS fetched its own code as vertices. A store-back
   diagnostic (below) showed it plainly: the buffer at the vertex address
   held the VS's words. Fixed in the probe; nothing in the driver code was
   wrong.
2. **After the fix, twice, PASS.** `GouraudBad=0`, all 1,024 pixels on the
   ramp. Row 0 reads FF028040 at x = 0, FF818040 at x = 32, FFFD8040 at
   x = 63. The fence landed after 10 polls.
3. **The committed build, PASS** (`sicp-gouraud-final`), and `/rect`
   again with the same build, PASS, BF4080FF (`sicp-rect-final`).

V9XBLTX afterwards: byte-exact, no engine timeouts.

## The diagnostic that was removed

To find run 1's fault, a `/fetchdump` mode had the VS store what its two
loads returned through a second V# (s4-s7) into a spare 4 KiB of the
window. Its first run showed the vertex buffer full of shader code, which
found the bug. After the fix, its dump area read back as the VS's first
48 code dwords. That is neither the guard the CPU wrote nor the vertices
the stores should have written, and it is not explained (`sicp-fetchdump-2`). A
diagnostic whose own output is not understood proves nothing, so it was
taken out of the probe rather than committed. MUBUF stores on this chip
are therefore **not** shown to work; nothing here depends on them.

## What this settles

- SI's TL vertex path, MUBUF loads through a V# in user SGPRs, works as
  built, with the V# above.
- The translated GCN 2 words for the TL VS's tail (`v_cvt_f32_ubyte*`,
  `v_mul_f32` with a literal) and the Gouraud PS (`s_mov_b32` to M0,
  `s_nop`, `v_interp_p1/p2_f32`, `v_cvt_pkrtz_f16_f32` as VOP3) run on
  GCN 1. With the rectangle, that is every instruction the Gouraud and
  solid programs use.

## Next

Textures (`image_sample`, the T# and S#) and Z16 depth: V9XCK3D's
`/texture`, `/modulate` and `/depth`. They use the same TL VS, so they
need no more SI shader work unless the PS or the descriptors differ on
GFX6.
