# External Windows 98 DDK inputs

Status: local ABI/toolchain reference
Recorded: 2026-08-08

The Windows 98 DDK is installed at `C:\98DDK`; matching retail symbols are at
`C:\win98retailsymbols`. These directories are external, read-only project
inputs. Their files must not be copied into source control or release packages.

## Selected input hashes

| File | SHA-256 |
|---|---|
| `License.txt` | `89F90F047AA98C71A405BF75A377CA075A6DFAB3540692E0E6A65655EE2B4019` |
| `help\OTHER.CHM` | `E9C3E8EC6E5042661DBD6C04DD762238A33ECE80B295013C1F9A37CD5E283D6F` |
| `inc\win98\MINIVDD.H` | `78B562F4BEF879B0637F5CECED2A4BDA3407D5BC340DBB54264C93B3B8F6DC9E` |
| `inc\win98\MINIVDD.INC` | `14492B744EBB954156E8E8C6442DE21622711E6BD94AC0AB3781B3074F484EE6` |
| `inc\win98\inc16\VALMODE.INC` | `D54A66E426459B82A8B1A48E91594D9E962D9C6EB3FF4622BC331E6E43955AC3` |
| `lib\win98\DIBENG.LIB` | `235FA8DF800C17059880B3442035AA2C038537BB75DD458E4D2C3AF7717DF055` |
| `bin\win98\ML.EXE` | `33455C7E38348FFDA7B73EB66B818F0C00E5D8D52F08EAE665616E3A5C9C9B8B` |
| `bin\LINK.EXE` | `62A80AF374052F78C82D7E1407B662646FC80425AA38B63EA6D67EE0FF3CD259` |
| `bin\win98\ADRC2VXD.EXE` | `1AA5E83D72E222C8C8DA6509CAD92BDB0E3460DB7C9CFB5501C16590733646DA` |
| `src\display\mini\mini\MINI.DEF` | `3488CBA62912A8E4DA5C07BF39CC0941AC43342B868FA8199E04119E8B298A27` |
| `src\display\mini\mini\DIBLINK.ASM` | `47A779D0BBE3136D68284B866632F253060274AAAE7FDAFB393C06E56A9127A0` |
| `DIBENG.SYM` | `70FCF2BAAA915A8FF95BBA7FE23834C3AA49954197E496AA459358B68790719E` |
| `VDD.SYM` | `D5380AE36734E266801C50304FDE480BFB17F836CBF495021AD4F3A200961E4C` |

`DIBENG.LIB` is an import library: the build uses it to describe imports from
Windows' `DIBENG` module. It is not linked into or redistributed with the DRV.

The mini-VDD build invokes the external DDK copies of `ML.EXE` and `LINK.EXE`.
No DDK executable, header, library, sample, or symbol file is copied into the
repository or release output.

## Confirmed architecture

The DDK documentation confirms that the project needs a 16-bit display
minidriver, the system DIB Engine, a hardware-specific mini-VDD, the system VDD,
and optionally VFLATD for banked apertures. For a linear framebuffer, the
display minidriver creates and owns the selector; the mini-VDD handles VGA
virtualization and screen switching rather than mapping that selector.

The source files in the DDK are consulted only to confirm ABI names, ordinals,
and build behavior. Velocity9x source must remain independently written.

## Consultations on record

Reading the DDK to develop a driver is what its EULA grants and what this
document allows. Anything read that changed a decision is listed here, so that
provenance is a matter of record rather than of memory.

| Date | Files | What was taken |
|---|---|---|
| 2026-09-02 | `src\display\mini\s3v\` - `VIRGE1.H`, `D3DRENDR.C`, `D3DDRV.C`, `DDDRV.C`, `VGA.ASM`, `INIT.ASM`, `S3DATA.H`, `S3_DD32.C` | That the S3D engine's 3D command word has no RGB565 destination format, that S3's driver selects the destination on bit depth alone, that a `HighColor` key in `SYSTEM.INI` selects a 5:5:5 desktop, and the capability set S3 published for this silicon. Recorded in [`decisions/2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md`](decisions/2026-09-02-s3d-writes-1555-because-it-can-only-write-1555.md). |

| 2026-09-03 | `inc\win98\DDRAWI.H` | The field order of `DBLNODE`, the surface attachment-list node, mirrored as `V9X_DD_ATTACH_NODE` so the ViRGE engine can walk a texture's mip levels. ABI shape only, the same use the surface structs in `win9x_ddraw_abi.h` have always made of that header. |

Two things about that entry are worth stating plainly.

**The rule above was applied late.** The first version of that decision document
and of its commit message quoted DDK source directly - four preprocessor lines,
a short C switch, and seven lines of assembly with their comments. Those were
replaced with descriptions and file-and-line citations on the same day, but the
original text was committed and pushed first, so it remains in history. The
check belongs before the read, not after the write.

**Independence is now a live question for one file.** `d3d_virge.c` builds the
same command word S3's driver builds, and did so before any of this was read -
that agreement is the finding. But the ViRGE Direct3D path has now been read
about, and "independently written" for later changes to it means something
weaker than it did for the rest of the tree. The capability list above is a
list of what the silicon can do, which is fact; turning any of it into driver
code is where the distinction has to be argued rather than assumed.
