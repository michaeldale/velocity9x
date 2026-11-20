# DDI 8 on the HD 3000: Gen6 pixel shader 1.1 kernels, DXT, cubes, two stages

Date: 2026-11-20
Status: **measured, M-NZHWBR boots 65-68.** Step 8 of
`docs\plans\intel-sandybridge-direct3d.md` (PS 1.1, two stages). Builds
`98ecdff7-dirty` (branch `intel-snb`). Evidence:
`docs\probe\m-nzhwbr-snb-hd3000-ddi8-ps11-2026-11-20\`.

## What changed

- `hsw_hw16.c` stamps `CAP_D3D_DDI8` on Sandy Bridge, so DirectX 8 and 9
  programs get DDI 8 (over DDI 6, as Gen3 does) and with it pixel shader
  1.1, DXT and cube maps.
- `hsw_ps11.c` builds Gen6 kernels (`v9x_intel_snb_ps11_build`): Gen7's
  instructions in Gen7's registers, each send's payload first moved to m1
  on and sent from there with Gen6's function control, and texkill done
  through the render target header (below).
- `snb_3d.c`: the PS 1.1 draw batch (`v9x_intel_snb_draw_workload_ps11`:
  wide vertices in eight elements, SF and WM for six attributes, five
  binding entries, CONSTANT_PS as an offset from the dynamic state base);
  BC trees padded to an even row of blocks; cube trees spaced h0 + h1 +
  11 j for any level count (Gen7's is 12 j, and Gen6 has no LOD0
  spacing), two rows of padding after the sixth face; a cube surface.
  These follow the SNB PRM Vol 4 Part 1's surface padding rules and
  Mesa's isl, recalled, not read here.
- The HAL picks the Gen6 forms by `CAP_GEN6_SNB`; Sandy Bridge's limits
  get DXT and two texture stages.

## Measured

| Boot | Build change | V9XD32 T19 |
|---|---|---|
| 65 | DDI 8, Gen6 PS 1.1, predicated EOT kill | 16 rows mismatch (old probe); with the probe fixed, Kill and KillFog and the four two-stage rows |
| 66 | kill via header, flag as src1 | Kill: nothing written at all |
| 67 | flag as src0 | Kill, KillFog pass; four two-stage rows left |
| 68 | two texture stages | **PASS**: every row but FfBemL8/L6 (skipped, no luminance op) |

Boot 68, the build to keep:

- V9XD32 default and `/ddi6`: PASS, T12 (two stages) included. The
  release probes (V9XD32 default, `/bpp16`, `/ddi6`, `/ddi5`, GL, Glide,
  R3DP, WPRS, FBCMP) all pass.
- 3DMark 2001 SE, the owner's saved settings (1024x768x32, Low detail,
  software T&L), vsync default: **3519**, in 7.0 minutes. Its settings
  line now reads DXTC. Counters across the run: no engine idle timeout or
  reset, no refused batch, `Ps11KernelsBuilt` +9, none refused or
  dropped. The ten grabs cover Car Chase only and show it drawn as
  expected at contact-sheet size; Nature was not grabbed.
- 3DMark 2000, 1024x768x16, the release check run twice: cut at its
  default 3 minutes (1,053,107 draws), then whole: **4068**, in 5.4
  minutes, 1,183,636 draws, none refused, no stall, nothing drawn into
  the presented buffer. Two stages stopped an HD 4000 (Gen7) in Game 1
  within a minute (`docs\issues\2026-11-05-ivb-mobile-hang-in-3dmark2000-
  game1.md`); the HD 3000 ran both game tests through. Its ten grabs
  (`release-checks\hd3000-before\3dmark2000-contact-sheet.png`), all of
  Game 1, show terrain, trees, the helicopter and the HUD drawn as
  expected at contact-sheet size.

## What the runs showed

- **V9XD32 T19's reference read an unset field.** `V9X_PS11_ENV.unbound`
  came after `d32_t19_env` was written and was never set; `env` is a
  local, so the reference took stage 0 as empty, (0, 0, 0, 1), wherever
  the stack held an odd word there. Boot 65's first run "failed" 16 rows
  that way: the pixels drawn decode exactly as the right texels times
  the right colours (Kill's last bad pixel is texel (0, 7), FfSpec's is
  texel (0, 0) x diffuse + specular). The GeForce 210's 0.54.0 run at
  X8R8G8B8 failed the same rows with the same first pixels (FourTex
  6D3645h, Kill 5858E0h, FfSpec 6B92FCh), which
  `2026-11-17-0.54.0-release-checks.md` put down to the GT218: it is
  very likely this. The probe now sets the field, and records the last
  bad pixel and the x87 status and control words per row (status 0120h,
  control 007Fh: no stack fault, D3D8's single precision).
- **Gen6 does not kill by predicating the EOT write.** Boot 65 drew all
  335 of Kill's killed pixels. Mesa's Gen6 discard ANDs the flag into the
  pixel mask copy, which the render target write takes from its header:
  the PRM's PS payload R1.7 low word (the high word is the dispatched
  mask, not to be modified) and the write header's M1.7. With the flag
  as the AND's src1, boot 66 wrote no pixel; with it as src0, as Mesa
  has it, boot 67 passes. Why src1 read differently is not known.
- **One texture stage under DDI 8 dropped stage 1.** DDI 8 tells an
  application four blend stages wherever shaders run, and ValidateDevice
  passed FfSpec2, FfTwo, FfBem and FfBem3 in one pass, but with
  `texture_stages` 1 the core kept stage 0 alone. Two stages is
  Haswell's setting; Ivy Bridge keeps one for its hang. Gen6's kernels
  had drawn two units for the ICD (Quake 3, boot 64).
- **Gen6 cubes** pass T19 Cube (one level) at the 11 j spacing and the
  PS 1.1 path; Gen6 DXT1 and DXT3 pass T19 Dxt1 and Dxt3.

## Not measured

- Nature's frames, the pixel shader feature tests' frames.
- Cubes with mips on Gen6 (T19's cube has one level), and BC cubes.
- Ivy Bridge's own two-stage hang: nothing here bears on Gen7.
