# Rage XL follow-ups: 4x4 and 2x2 textures, the mode sync, the DRV version, oversized GL textures

Date: 2026-10-03/04. Machine: A8U4I5 with the ATI 3D Rage XL PCI
(`1002:4752`, 8 MB), Mach64 engine path. Evidence:
`docs/probe/a8u4i5-rage-xl-pci-2026-10-03/` (`fixes-boot/`,
`hl-d3d-mwd5-smalltex/`, `q2-gl-timedemo-fixes/`).

Michael was away from the machine, so only changes that could not hang it
were made: no new draw class reached the engine except textures under 8
texels, which a probe scene measured first.

## Textures with edges of 4 and 2

Half-Life refused 357 draws a run for their shape, the last one 4x4
(`M64ShapeRefusedSize=0x00040004`). The policy and the state builder
both stopped at 8, but nothing measured put the limit there, and the
builder already programs mip levels down to 1x1.

V9XDDP's halves scene gained 4x4 and 2x2 entries: green on the left
half, blue on the right, each half drawn and read back. With the policy
alone lowered, both read 0. The builder still refused them
(`mach64_engine.c`, four draws refused in the HAL), so the chip was
never asked. With the builder at 2 as well:

| | Left | Right | Ok |
|---|---|---|---|
| Tex8 | 2016 (0x07E0) | 31 | 1 |
| Tex4 | 2016 | 31 | 1 |
| Tex2 | 2016 | 31 | 1 |

(`fixes-boot/V9XDD-SMALLTEX.INI`.) The policy, the builder and the engine
limits now take edges from 2 to 256. 1x1 has no halves scene and stays
refused. Half-Life afterwards ran 14.843, 18.125 and 18.141 fps (recorded,
not compared) with no shape refusals (`hl-d3d-mwd5-smalltex/`).

## The mode-list sync with two marked instances

With the Rage IIC's display class key still marked `V9xFamily=ati`
after the swap, V9XSYNC refused to choose
(`multiple-marked-instances`). HKLM\Enum keeps the removed card's
devnode, so it cannot tell which key is live. The sync now looks up the
devices present this boot under `HKEY_DYN_DATA\Config Manager\Enum`,
follows each one's `HardWareKey` to its `Driver` value, and writes only
when exactly one marked key is in use. A dry run, then the boot run, on
A8U4I5: `MarkedInstances=2`, `Chose=live-instance`, `Instance=0007`, 7
baseline modes kept and 12 BIOS modes added, `Status=ok`
(`fixes-boot/V9XSYNC.INI`).

## The driver's version in DxDiag

DxDiag showed `Driver Version:  ()` on the reporter's machine and here.
V9XDISP.DRV had a version resource (wdump: type 16, 372 bytes) that
neither DxDiag nor Windows' `GetFileVersionInfo` could read. In it,
InternalName and OriginalFilename each declared 4 bytes more than they
held, and the parents inherited the 8. wrc's 16-bit output declares
every string value one byte longer than it writes. With the `.rc` text's
explicit `\0` it writes one terminator; without it, none at all. Either
way the extra byte shows wherever it crosses a 4-byte boundary. Dropping
the `\0` fixed the ATI driver's strings and broke the Intel driver's, and
the build check below caught that. V9X16LD.EXE had the same fault; the
32-bit binaries do not. So for a Win16 image the build lays out the
16-bit `VS_VERSION_INFO` itself (`New-V9xVersionBlock16`) and gives it to
wrc as raw data of type 16. `Assert-V9xVersionResource` now fails a build
whose resource Windows cannot read. Every family's DRV and the loader
read correctly on the host. DxDiag on A8U4I5 reads
`Driver Version: 0.10.0000.0000 (English)` (`fixes-boot/DXDIAG-FINAL.TXT`).
`Mini VDD Date` is still blank: not looked into.

## GL textures past the device's largest edge

The reporter's log had six draws returned INVALID with a 512x256 texture
bound. The issue filed for them blamed a vertex on the 640x480 edge;
that was wrong. `v9x_r3d_validate_levels` refuses a level past the
engine's `texture_size_max`, which is 256 on the Mach64 and the Rage
IIC. The ICD accepts 512, and drops a draw answered INVALID; it falls
back to the CPU only on UNSUPPORTED. The ICD now fits the texture before
the draw (`v9x_gl_tex_fit`, host-tested): a mipmapped chain loses levels
from the top, and a single level is box-filtered to fit and kept on the
object until its next image. Not seen on hardware: neither Quake 2 run
here bound a texture over 256 (0 INVALID draws before and after).

## Quake 2's refused batches named

A new counter for the engine's passive check (`M64AcceptPolicyNN`,
ABI 2026100306) shows Quake 2's refusals on the Rage XL
(`q2-gl-timedemo-fixes/`): 6,441 `TEXTURE_OP` on the hardware texture,
then the same 6,441 `TEXTURE_FORMAT` on the CPU copy, which the Mach64
has no route for. 6,507 batches, 385,721 triangles, are not drawn. The
op is refused by the policy as one the chip cannot express. That is the
blend by texel alpha times vertex alpha already open for the Rage IIC
(`docs/issues/2026-10-03-rage-iic-texel-times-vertex-alpha-refused.md`),
and it is not fixed here.
