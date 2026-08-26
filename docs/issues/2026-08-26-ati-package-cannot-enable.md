# A freshly built `ati` package cannot enable: no collection and no aperture

Status: **fixed 2026-08-26.** `MiniVddVbeCollect` is `$true` for `ati` again,
which is what the 2026-08-18 gating decision always said a family with no
`read_aperture` hook must have. Two assertions now make the pairing
unrepresentable rather than merely documented - see "How it is prevented now" at
the end. Found on the Mach64 VT2 guest while regression-testing something else.

Target: `Win98SE-Mach64VT2`, 86Box, ATI Mach64 VT2 264VT2 (`5654`), reached at
`127.0.0.1:9873`.

## What happens

Build the `ati` package from `main` and install it. The driver never enables:

```
Stage=fail-hardware-aperture
VbeDetail=minivdd-no-mode
VbeCache=s=0 l=0 q=0 c=0 p=0 f=0800
```

Windows falls back to the INF's 4-bpp `vga.drv` row at 640x480 and the desktop
comes up on stock VGA.

## Why

`f=0800` is `V9X_VBE_ST_COLLECT_OFF`, and no other status bit is set: the
mini-VDD shipped with its boot-time VBE collection assembled out, so the 4F9Ch
cache is empty by construction. That is exactly what
`packaging\families\ati\family.psd1` asks for:

```powershell
# Stage 1's many-mode BIOS walk rolls out to QEMU VBE first. ATI keeps
# the mini-VDD for power callbacks but assembles collection out until
# its own guest/physical inventory gate is ready.
MiniVddVbeCollect = $false
```

**But `ati` has no `read_aperture` hook.** `src\chipsets\ati\ati_hw16.c` mentions
one only in the past tense ("the native read_aperture hook once..."), and the
family table leaves the slot null, so `v9x_vbe_default_aperture` - the tier-0
path - is the only way this family can learn where its framebuffer is. That path
reads the 4F9Ch cache. With collection off the cache is empty, the aperture read
returns zero, and the Enable sequence refuses at stage 3.

This is the exact case
`docs\decisions\2026-08-18-minivdd-vbe-collect-gating.md` §2 carved out and told
us not to do:

> Tier-0 families keep the collection because they have no other way to learn
> the aperture

`s3` and `matrox-m2` can safely set `MiniVddVbeCollect = $false` because they
have `read_aperture` hooks. `ati` and `vbe` cannot. The manifest setting and the
decision contradict each other, and the manifest is the one that is wrong.

## Confirmed by reversing only that one bit

Same tree, same DRV, same guest - only the mini-VDD rebuilt with
`build-minivdd-skeleton.ps1 -Family ati -DisableVbeCollect:$false`:

```
Stage=enable-ok
VbeDetail=ok
VbeCache=s=1829 l=32 q=32 c=22 p=0 f=0107
VbeController=v=0200 mem=64 caps=00000000 rev=0100
```

22 modes cached off a VBE 2.0 BIOS, and the driver enables. So the collection
switch is the whole of it.

## Why nobody noticed

The guest was running an installed binary from **before** the manifest changed,
and it worked - `Stage=enable-ok`, `VbeDetail=ok`, 1024x768x16. Nothing rebuilds
and reinstalls the `ati` package unless someone is working on that family, so
the break has been latent since the manifest was set. `git log` puts the change
in the Stage 1/2/5 pipeline work; it predates 0.5.0.

Worth noting what would and would not have caught it: `run-checks` builds the
`ati` package and passes, because a package that builds is not a package that
enables. `run-vm-mode-matrix.ps1 -Family ati` would have caught it immediately -
it checks `Stage=enable-ok` after a reboot - but it is not part of `run-checks`
and needs the guest running.

## The fix

Option 1 below was taken. Both were coherent; option 1 restores the documented
decision, is one manifest value, and needed no new hardware verification -
the collection is how this family has always learned its aperture.

1. **Set `MiniVddVbeCollect = $true` for `ati`.** Restores the 2026-08-18
   decision as written. The stated reason for turning it off was to stage the
   Stage 1 BIOS walk on QEMU VBE first, which is a sequencing preference, not a
   safety requirement - and the collection is what this family has always used
   for its aperture, so this is not new risk.
2. **Give `ati` a `read_aperture` hook.** The comment in `ati_hw16.c` says the
   aperture is "exactly its BAR0", so a PCI-config read would do it, and the
   family would then genuinely not need the collection. More work, and it wants
   its own verification on both ATI targets.

## All four families, checked

`ati` is the only one affected, so this is a one-family defect rather than a
systemic one:

| Family | `MiniVddVbeCollect` | `read_aperture` hook | Verdict |
|---|---|---|---|
| `ati` | `$false` | **none** | **broken** |
| `s3` | `$false` | `v9x_s3_read_aperture` | fine |
| `matrox-m2` | `$false` | `v9x_mga2_read_aperture` | fine |
| `vbe` | *key absent* | none, and does not need one | fine |

`vbe` is safe by accident of omission rather than by decision: the key is not in
its manifest at all, and `build-active-package.ps1` reads it as
`($familyManifest.Build.MiniVddVbeCollect -ne $false)`, so an absent key means
the collection stays on. That works, but it means the one family that must never
have collection disabled is protected only by nobody having typed the key. A
build-time assertion - a family with no `read_aperture` hook may not set
`MiniVddVbeCollect = $false` - would have caught this defect and would stop the
`vbe` case ever becoming one.

## How it is prevented now

The rule was already written down - the comment above the manifest check in
`scripts\lib\family.ps1` has always said the collection is "what tier-0 families
(no read_aperture hook) require". It was advice. It is now two assertions built
on one shared detector, `Test-V9xFamilyHasApertureHook`, which reads the ops
table's initialiser line out of the family's own sources so the manifest cannot
drift away from the code:

- **`Test-V9xFamilyManifest`**: a family that sets `MiniVddVbeCollect = $false`
  and does not fill the `read_aperture` slot is rejected. That is this defect.
- **`check-tree.ps1`**: a family that *does* fill the slot and leaves the
  collection on is also rejected - the other half of the 2026-08-18 decision,
  which had never been asserted at all.

`check-tree.ps1` used to say "enabled only for vbe". That hardcoded list is what
held this bug in place: it made the broken state the *required* state, so the
check that should have caught the defect defended it instead. The allowed set is
now derived from the hooks rather than listed. Its original concern - that a new
family should not acquire boot-time BIOS calls by accident, since an absent key
means enabled - is still met, and better: a new family with a hook is rejected
for enabling, and one without a hook cannot work at all unless it is enabled.

The `vbe` family was safe only by accident of omission before this, since its
manifest has no key at all. It is now safe by assertion.

## Verified after the fix

On `Win98SE-Mach64VT2`, from the manifest-driven package (no hand-built
binaries):

- `Stage=enable-ok`, `VbeDetail=ok`, `VbeCache=s=1829 l=32 q=32 c=22 p=0 f=0107`,
  `VbeController=v=0200 mem=64 caps=00000000 rev=0100`.
- `run-vm-mode-matrix.ps1 -Family ati -ChipId mach64-vt2` passes all six
  declared modes.
- The dynamic pipeline came on with it, which is the point of Stage 6 and is
  recorded there: `Table=rows=15 published=15`, seven baseline rows plus **eight
  dynamic**, `Scan=state=1` (trusted for hiding), and a real panel EDID at
  `v=0104 preferred=800x600 ext=1`.
- Two of the eight newly discovered modes were driven by reboot - 1280x1024x8
  and 320x200x8, the largest and the smallest - and both reach `enable-ok` at
  exactly the requested geometry.
