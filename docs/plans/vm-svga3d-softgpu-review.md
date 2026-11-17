# VM acceleration: SoftGPU review and recommended path

Date: 2026-11-17. Status: research and proposed implementation only. No VM
was booted, no acceleration was measured, and no driver code was changed.
Upstream documentation and selected source files were inspected, not a full
source audit. Upstream links track their current branches; pin revisions before
implementation. The working tree already contained unrelated driver changes.

## Recommendation

Add a VMware SVGA3D backend to Velocity9x's existing rendering architecture.
Start on a pinned VMware Workstation configuration, then validate VirtualBox
VMSVGA independently. Treat protocol generation and supported commands as
runtime capabilities, not consequences of a PCI match or hypervisor name.

This recommendation assumes the objective is host-GPU acceleration for Windows
98 applications while retaining Velocity9x's HAL, OpenGL, Glide and 3DCIF
front ends. For simply playing games today, evaluating the existing SoftGPU
package is the shorter route. For physical PCI GPU assignment, see below.

## What SoftGPU actually supplies

SoftGPU packages VMDisp9x, Mesa9x, Wine9x, OpenGlide9x, VMHAL9x and a qemu-3dfx
fork. It offers CPU renderers, SVGA3D virtual-device acceleration, and a separate
QEMU graphics-call forwarding route. Its advertised API coverage is the result
of this stack, not a property of a display driver alone.
[Source: SoftGPU](https://github.com/JHRobotics/softgpu).

VMDisp9x provides the display and privileged device interface. Mesa9x supplies
OpenGL rendering through Mesa, including an SVGA backend. VMHAL9x supplies a
DirectDraw/Direct3D HAL using Mesa for rasterization; Wine9x is another component
of the package, so it would be inaccurate to describe all Direct3D support as
only Wine translation.
[VMDisp9x](https://github.com/JHRobotics/vmdisp9x),
[Mesa9x](https://github.com/JHRobotics/mesa9x),
[VMHAL9x](https://github.com/JHRobotics/vmhal9x).

The inspected `3d_accel.h` exposes command allocation/submission, fence
get/query/wait, region creation/free, capability queries and object-table setup.
`vxd_svga.c` implements ring-0 SVGA operations and tracks regions, contexts and
surfaces. These are useful reference boundaries, not a small drop-in DLL ABI
Velocity9x already implements.
[API header](https://github.com/JHRobotics/vmdisp9x/blob/main/3d_accel.h),
[SVGA implementation](https://github.com/JHRobotics/vmdisp9x/blob/main/vxd_svga.c).

SVGA3D gives a guest access to host GPU rendering through a virtual device.
The host retains control of the physical GPU; this is distinct from assigning
a PCI GPU to the guest.
[Mesa's SVGA3D architecture](https://docs.mesa3d.org/drivers/svga3d.html).

## Alternatives

| Route | Fit and decision |
| --- | --- |
| Native SVGA3D backend | Best architectural fit for this project. Reuses existing API translation and targets VMware plus VirtualBox, with separate validation. Requires substantial resource and transport work. |
| Integrate Mesa9x and its device ABI | Consider if broad OpenGL/API coverage is the overriding goal. Brings a renderer, build/runtime dependencies and another resource model. Benchmark a separate prototype before choosing it over the native backend. |
| qemu-3dfx forwarding | Candidate for a QEMU-specific experiment. Requires a patched host and compatible guest bridge. SoftGPU documents signature matching; its renderer matrix lists fullscreen and context restrictions. Not the general backend recommendation. |
| Virtio-gpu + virgl | A possible long-term QEMU route. Requires Win9x virtqueue/device support, resource transfer, synchronization and a virgl command/shader producer. This repository has no such backend; not a configuration-only addition. |
| Physical GPU assignment | Separate workstream: exercise an already supported physical card under a suitable hypervisor and audit discovery, BAR mappings, BIOS initialization and reset. SoftGPU's virtual-device path does not solve these requirements. |

Sources for the QEMU alternatives:
[SoftGPU's QEMU integration](https://github.com/JHRobotics/softgpu/blob/main/qemu.md),
[renderer restrictions](https://github.com/JHRobotics/softgpu#feature-support),
[QEMU virtio-gpu documentation](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html).
QEMU supports accelerated virtio devices generally; SoftGPU's lack of that
guest path must not be restated as QEMU having no 3D acceleration at all.

## Where it fits in Velocity9x

The current code already supplies the useful integration points:

- `src/display32/r3d/r3d.h`: neutral draw descriptions and shared geometry.
- `src/display32/d3d/d3d_internal.h`: `V9X_D3D_ENGINE_OPS`, capability limits,
  batched draws and surface lifecycle hooks.
- `src/display32/d3d/d3d_core.c`: engine selection.
- `src/display32/ddhal_internal.h` and `engines/`: 2D operations and scanout.
- `packaging/families/` and `src/common/backend_registry.c`: family manifests
  and generated device registry. The latter is host policy, not the actual
  16-bit enable path; both paths need appropriate integration.
- `src/minivdd32/`: privileged mappings and services.

Proposed flow:

    Existing D3D / OpenGL / Glide / 3DCIF front ends
        -> neutral render work
        -> SVGA3D state and command encoder
        -> bounded submission + resource/fence services
        -> hypervisor renderer -> host GPU

Use a separate `svga` family with backend, 2D engine and 3D engine modules.
Keep packet construction and validation host-testable. Determine the privileged
service split against `docs/specifications/win9x-driver-boundaries.md` before
changing module contracts. The proposed render-service/replay work is useful,
but completing that entire refactor should not block an isolated device probe.

The largest design change is surface coherence. The neutral surface currently
contains a video-memory offset and a front-end object; a host SVGA resource
needs its own stable identity and lifetime. Model CPU staging storage separately
from host surfaces. Track which copy is current, upload dirty regions, wait
before CPU reads, and defer resource reuse until submitted work completes.

Software fallback also needs this contract: a CPU draw after a GPU draw must
see current colour and depth, and subsequent GPU draws must see CPU changes.
Do not silently draw into an obsolete staging copy. Start with a constrained
feature set and explicit refusal where coherent fallback is not yet supported.

## Implementation gates

1. **Inventory and baseline.** Pin hypervisor version, virtual hardware version,
   host GPU/driver, guest patches and SoftGPU revision. Record PCI resources,
   SVGA version, FIFO limits, 3D capabilities and resource limits. Establish a
   SoftGPU baseline and confirm an SVGA3D renderer rather than software fallback.
   Test with 3D disabled as well. No claim that current versions were tested here.

2. **Transport and first pixels.** Build a standalone guest probe before the
   full HAL integration: bounded FIFO submission, fences, context/surface
   creation, clear, triangle, readback and present. Prefer the simpler legacy
   command path only if the chosen host advertises and executes it correctly.
   If only guest-backed/DX-style commands are viable, decide that explicitly
   before writing the full renderer. A device ID alone is never sufficient.

3. **Display and DirectDraw.** Add the family, start with a 32-bpp desktop,
   mode switching, visible-region updates, fill/copy, locks and recovery.
   Implement presentation deliberately rather than assuming physical CRTC
   flips or real scanline timing. Validate fullscreen return and DOS transitions.
   Add 16-bpp application surfaces and conversion in a separate measured step.

4. **Shared 3D engine.** Implement clear, Gouraud triangles, textures, depth,
   alpha test/blend, fog and clipping. Publish only validated limits. Integrate
   existing front ends, then extend multitexture and shaders independently.
   SVGA support alone does not expand Velocity9x's front-end API coverage.

5. **Lifetime and performance.** Test lock/readback, texture mutation, mixed
   CPU/GPU drawing, multiple contexts/processes, mode changes, application death
   and fence timeout. Batch submissions; avoid a full GPU-to-CPU frame copy on
   every present. Record draw, upload, readback, submit and wait timings.

6. **Second hypervisor.** Repeat on a pinned VirtualBox VMSVGA configuration.
   Share protocol code but retain measured capability differences. SoftGPU's
   setup guidance uses VMSVGA, 3D enabled and 128 MB video memory; use it as a
   baseline recipe, not proof of compatibility for an untested release.
   [VirtualBox guidance](https://github.com/JHRobotics/softgpu/blob/main/vbox.md).

Use existing V9XDDP, V9XD32, V9XGLP and V9XGLIDP probes, then Quake 2,
Half-Life and 3DMark workloads. Compare pixels as well as speed against the
software engine and SoftGPU on the same VM configuration. Host tests should
cover packet bounds, FIFO wrap, fence ordering/wrap and resource state changes.
Run repository build/audit gates when code is introduced; none were necessary
for this documentation-only review.

## First deliverable

A read-only SVGA capability survey, followed by an opt-in first-triangle probe
with pixel readback and bounded completion. Its result determines the protocol
generation and resource model before committing to the full backend.

Preserve source provenance for any borrowed implementation. The inspected
VMDisp9x API and SVGA files carry MIT notices; the SoftGPU package comprises
separate projects and must not be treated as having one blanket source license.
Keep imported code distinguishable from Velocity9x's independently written code.
