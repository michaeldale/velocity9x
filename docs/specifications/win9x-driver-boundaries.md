# Windows 9x driver boundary specification (working draft)

Status: DDK-backed ABI baseline; active candidate host-audited
Target: Windows 98SE, S3 ViRGE/DX 86C375

## Proven project-owned boundary

The portable core owns validation, capability policy, diagnostic record layout,
and chipset dispatch. It accepts flat scalar values and project-owned structs;
it does not include Windows or DDK headers. Code shared with a 16-bit component
must remain C89-compatible and must not assume that `int` or pointers are 32
bits.

The first hardware backend recognizes only PCI vendor `5333` and device `8A01`.
Recognition alone grants no capability. Resources, aperture sizes, VRAM, and a
mode must be validated before the framebuffer capability can be enabled.

## 16-bit display DRV boundary

The display component will own the Windows display DDI entry points and DIB
Engine interoperation that empirical testing proves cannot live elsewhere. It
will translate segmented pointers and Windows-owned structures into the small
project-owned contracts in `include/velocity9x`.

The Windows 98 DDK establishes these requirements:

- `Enable` is ordinal 5 and is called for both `GDIINFO` and `PDEVICE` setup;
- `Disable` is ordinal 4 and releases selectors and allocations;
- `ReEnable` is ordinal 31 and handles dynamic mode changes;
- `ValidateMode` is exported by name, conventionally at ordinal 700, and must
  validate without changing the screen;
- drawing entry points occupy ordinals 1 through 30 and may forward to DIBENG;
- cursor entry points occupy ordinals 101 through 104;
- `ResetHiResMode` is a fixed-segment callback registered with the system VDD;
- a linear framebuffer selector is created by the 16-bit display minidriver.

The first active candidate now implements:

- the two-stage `Enable` contract and fixed-mode `GDIINFO` construction;
- DIB Engine PDevice, palette, access callbacks, and extended forwarding thunks;
- VBE 0x101 mode entry after exact read-only PCI BIOS identification;
- S3 aperture validation, DPMI mapping, selector setup, and teardown;
- master-VDD registration of the visible framebuffer and fixed
  `ResetHiResMode` callback, followed by post-mode state save and symmetric
  unregister/VGA-trap restoration.

`ReEnable` implements live same-depth mode switching on the vmdisp9x pattern:
it re-reads the registry-selected mode through `VDD_GET_DISPLAY_CONFIG`,
rebuilds the PDEVICE in place between `DIB_BeginAccess`/`DIB_EndAccess` with
cursor exclusion, re-registers the new visible-byte count with the master VDD,
and preserves the realized 8-bpp palette. Color-depth changes are refused
(Windows 9x never changes depth dynamically, KB Q127139; the 8-bpp PDEVICE is
also 1 KiB larger than the 16-bpp one) and follow the reboot path instead.

The registration ABI is host-audited, but DOS/full-screen switching through
the master VDD defaults remains guest-unvalidated. The source is an activation
candidate, not a release driver, until the cold-backed-up guest test and
standard-VGA recovery test pass.

## 32-bit mini-VDD boundary

The mini-VDD will own only responsibilities established by the Windows 98SE
mini-VDD contract and the spike. Candidate responsibilities are adapter/resource
discovery, screen-state transitions, mappings, engine synchronization, logging,
and recovery. The final split must reflect observed DDI constraints rather than
the preferred architecture in the plan.

The LE candidate has a valid descriptor/control path, verifies the master VDD
dispatch-table ABI, and logs at ring 0. It registers legacy `VESA_SUPPORT` and
VESA post-processing callbacks plus the Windows 98 monitor-power state and
capability callbacks. It advertises D0 only: active monitor low-power states
remain disabled until the legacy VESA resume path can restore the framebuffer
reliably. The following remain unresolved for later phases:

- interaction with the system VGA VDD and display DRV;
- DOS/full-screen transition callbacks and state save/restore;
- device-specific virtualization and later hardware recovery callbacks.

The master VDD retains its default handlers for every other callback.

## DirectDraw boundary (updated 2026-08-11)

A DirectDraw HAL is now present. The 16-bit driver carries only the ABI
glue the platform requires (DCICOMMAND escape dispatch in
`src/display16/dd16.c`, the DPMI shared block, 16:16 pointer stamping, and
the `lpDDHAL_SetInfo` call); all DirectDraw content and runtime callbacks
live in the flat 32-bit `V9XHAL.DLL` (`src/display32/ddhal.c`), loaded at a
fixed shared-arena base with shared PE sections. Advertised capability is
deliberately minimal: `DDCAPS_GDI`, one linear video-memory heap above the
visible screen, flippable primaries, real vertical-blank services, and
CRTC display-start page flipping, bounded engine synchronization, and
solid-colour ViRGE blits. The Direct3D v1 boundary exposes a minimal RGB
16-bpp device descriptor, allocation-free context lifecycle, and one
hardware-rendered primitive: flat-colour TL-vertex triangle lists through
`RenderPrimitive`. It does not expose lines, indexed primitives, textures,
Z-buffering, transforms, lighting, fog, blending, or clipping. The S3D output
format is currently native ZRGB1555 even though the active 16-bpp framebuffer
mode is RGB565; applications must not infer general 16-bpp Direct3D
compatibility from this pixel-verified milestone. Other blits, palettes, and
overlays remain with the HEL. See
docs/decisions/2026-08-11-directdraw-hal.md,
docs/decisions/2026-08-11-virge-engine-foundation.md, and
docs/decisions/2026-08-11-direct3d-phase3.md.

The paragraph above describes 2026-08-11 and is stale in most of its
Direct3D detail. One correction that crosses this boundary: since
hardware T&L step 6 (2026-10-27,
docs/plans/hardware-tnl.md), the HD 4600 HAL holds a Direct3D 7 T&L
device at DDI 7, not yet offered (`V9X_D3D_TNL_DEVICE_OFFERED` in
`d3d_core.c`, off while it is slower than the runtime). When offered, the
process sends object-space vertices, and the HAL builds the transform,
lighting and fog program for the engine's vertex stage (`d3d_tnl.c`,
`hsw_vs.c`). Until then, and on every other engine, the runtime
transforms and lights, and the HAL draws pre-transformed vertices.

## Previous DirectDraw boundary

No DirectDraw callbacks or capabilities are present in Phase 1. DirectDraw work
begins only after framebuffer mode stability. Its capability table must be
derived from operations that have passed conformance tests.

## Failure rules

- Unknown PCI IDs are rejected before hardware access.
- Integer overflow or insufficient VRAM rejects a mode.
- Optional capability bits begin clear.
- Every future hardware wait must be bounded.
- A driver-side failure must prefer an operable software/standard-VGA path.
