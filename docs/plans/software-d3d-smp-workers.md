# Mode 2 on a second CPU: banded rasterization through smp.vxd

Status: 2026-09-06. Planning only. Nothing here has been built or measured.
Every claim about the tree is a file:line citation; every claim about
smp.vxd is a citation into `C:\everything\smp.vxd` at commit `eeca7fa`.
Nothing about what an application processor actually does on a real board
has been observed by this project.

Parent: [`s3-trio64-voodoo2-hybrid-3d.md`](s3-trio64-voodoo2-hybrid-3d.md),
mode 2. This is an extension of that mode, not a fifth one, and it sits
behind that plan's work-order steps 1 and 2.

## The question

Can the software rasterizer draw on more than one CPU under Windows 98,
using Jaroslav Hensl's `smp.vxd`, and is it worth doing?

The first half is yes, at moderate cost, because the rasterizer already has
the shape that driver needs. The second half is unknown and is bounded by a
measurement that has not been taken, which is why the work order below
starts with a probe and a benchmark rather than with code in the engine.

## What smp.vxd is

Not an SMP kernel. Windows stays on the bootstrap processor. The VxD boots
each application processor into a small supervisor of its own
(`apboot.asm`, `apkernel.asm`) and moves one ring-3 thread at a time onto
it:

- A thread registers itself with `DIOC_SMP_ELEVATE`, naming a placeholder
  procedure and a per-thread lock word (`smp_proc.c:60`). The placeholder
  is what Windows schedules on the BSP while the real thread is away; it
  spins on the lock with `Sleep(0)` (`smp9x.c:192`).
- `fly()` is an `int 3` inside a fixed block of the kernel page. The VxD's
  hook copies the client register state, copies the current page directory
  into the AP's private copy, points the BSP thread at the placeholder and
  hands the state to a free AP (`taskswitch.c:385-489`, `:534-553`).
- On the AP the thread runs at ring 3 with a null FS and a snapshot of the
  page directory in which the kernel32 range `0xBFC00000-0xBFFFFFFF` is
  unmapped (`paging.c:59`). Any fault there, or any other exception,
  carries the thread back to the BSP and re-executes the faulting
  instruction. `land()` does the same on request.
- There is no preemption on the AP. A thread holds it until it lands or
  faults.

So the model is: a compute thread that spins on shared memory and calls no
Win32 API while flying. `README.md` says as much and says the whole thing
is a concept with large stability issues, tested on VirtualBox, VMware and
one Xeon board.

## Why the rasterizer fits

- `v9x_d3d_raster_triangle` is pure arithmetic over caller-supplied
  pointers. Its row loop (`d3d_raster.c:881`) calls nothing outside the
  file. No trace, no API, no chip register. That is exactly the code an AP
  can run.
- Rows are independent. A worker that owns a set of rows owns every depth
  and colour write on them, so two workers never race on a pixel and the
  per-pixel order of a batch is preserved. Blending and depth need no
  locking.
- The engine's batch call (`d3d_soft.c:746`) already converts every vertex
  before drawing. Each worker can walk the same batch and draw only its
  rows; nothing needs copying.
- The HAL is linked against KERNEL32 only, and the build script's audit
  allows exactly that (`build-ddraw-hal-dll.ps1:165`). `CreateFileA`,
  `DeviceIoControl`, `CreateThread` and `Sleep` are all in reach without
  loosening it. The smp.vxd client is a few dozen lines: open the device,
  three ioctls, and five function pointers at fixed offsets from
  `DIOC_SMP_GET_ADDRESS` (`smp9x.h:32-39`).
- A fork-join per draw call changes no fence. The engine splits the batch,
  draws its own share on the BSP, spins until the workers report done, and
  returns. Lock, Flip and GetBltStatus see a synchronous engine exactly as
  they do today. Nothing becomes asynchronous, so nothing in
  `ddhal_core.c` changes.

## What is harder than it looks

1. **The autorun scheduler will not fly HAL code.** It only moves a thread
   whose instruction pointer is in `0x00100000-0x7FFFFFFF`
   (`taskswitch.c:156`). The HAL is at `0xB0400000`
   (`build-ddraw-hal-dll.ps1:9`). Workers therefore use `SMP_MODE_MANUAL`
   and explicit `fly()`, and after every bounce they are on the BSP until
   they fly again. The worker loop has to treat "am I on an AP" as
   something to re-check and re-establish each time it picks up work, not
   as a state it was put in once.
2. **A fault mid-frame is silent.** A page fault or a stray API call drops
   the worker to the BSP, where it keeps running the same loop on the same
   CPU as the engine's own share. The join still completes, so the picture
   is right, but the frame ran on one CPU and nothing said so. The
   per-context state needs a counter of bounces, read by the trace, so a
   run that drew everything on the BSP is distinguishable from one that
   did not.
3. **The HAL is one instance across processes; threads are not.** Every
   section is shared (`build-ddraw-hal-dll.ps1:141`). Workers must be
   created in the owning process at `V9xD3dContextCreate`
   (`d3d_core.c:687`) and retired at `V9xD3dContextDestroy`
   (`d3d_core.c:753`) and `ContextDestroyAll`, and the join state must
   live in the context, not in a static.
4. **Small batches lose.** A DirectX 3 title through execute buffers can
   arrive one triangle per `RenderPrimitive` (`d3d_core.c:1235`). A
   cross-CPU wake and join costs more than a small triangle. The split
   needs a threshold, in triangles or in covered rows, below which the BSP
   draws alone; the threshold is a measured number, not a guess.
5. **The AP's page directory is a snapshot.** Taken at each `fly()`
   (`taskswitch.c:424`). The framebuffer aperture and the HAL are in the
   shared arena and should be present, but that is a reading of smp.vxd,
   not an observation. Step 1 below exists to observe it.
6. **A spinning worker is a spinning BSP thread too.** The placeholder
   yields with `Sleep(0)` in a loop at normal priority (`smp9x.c:192`).
   One worker is one extra runnable thread on the BSP for the life of the
   context. Whether that costs the engine anything measurable is a step 2
   number.

## What bounds the gain

- **No mode 2 target has a second CPU.** BARRY is a classic Pentium; the
  VLB machine is a 486; 86Box emulates one CPU. The only fleet machine
  smp.vxd would count as two is the Atom netbook, whose hyper-thread it
  reports as a CPU (`smp.c:232`), and that is an Intel GMA family
  machine. Everything below is therefore built and measured on a two-CPU
  VirtualBox or QEMU guest, and by this project's own rule a VM number for
  a CPU-bound path describes the emulator, not a target. The decision
  record has to say that in its first paragraph.
- **The aperture does not get wider.** The parent plan expects the
  software path on real hardware to be bound by uncached writes through
  the PCI aperture. Two CPUs writing into the same aperture share the same
  bandwidth. The second core pays only where per-pixel arithmetic
  dominates, which is the system-memory render target the parent plan's
  step 2 has not yet settled. Until that exists, the expected gain on the
  machines mode 2 is for is near zero, and this plan should not be
  executed past step 2 before the parent's steps 1 and 2 are.

## Design

Kept short because none of it is settled by a measurement yet.

- **Rasterizer.** Add a row band to `v9x_d3d_raster_triangle`, expressed
  as a stride and a phase: draw rows where `row % stride == phase`.
  Interleaving balances small triangles better than halves, and false
  sharing is confined to row ends, since the pitch is 8-byte aligned
  (`d3d_soft.c` limits, `target_pitch_align`). Stride 1, phase 0 is
  today's behaviour and every existing pixel test must pass unchanged. A
  new test draws each table entry with stride 2 and 3, unions the bands,
  and compares to stride 1 byte for byte.
- **Engine.** `v9x_d3d_soft_draw_triangles` converts the batch as now,
  then either draws it whole or publishes a work record (target, depth,
  texture, alpha, vertex array, count, stride) to the context's workers,
  draws phase 0 itself, and spins on a done counter. The workers are the
  only readers of the record and the BSP is its only writer; a release
  store on a generation counter is the hand-off and the done counter is
  the join. Volatile and `lock xadd` through `#pragma aux`; no
  `InterlockedIncrement`, because a worker cannot call kernel32.
- **Worker.** A thread per AP, created at context creation, that
  elevates once with `SMP_MODE_MANUAL`, then loops: fly, spin for a new
  generation, draw its phase, increment done, repeat. Teardown sets a quit
  flag, waits for the worker to land, and joins the thread. The worker
  never calls the trace and never touches the shared block's diagnostic
  ring. Anything it wants recorded goes into per-context counters the BSP
  reads afterwards.
- **Client.** A new module in `src/display32`, say `smp_client.c`, that
  owns the device handle and the five function pointers and answers
  "how many APs" and "am I on the BSP". Absent VxD means zero workers and
  the engine behaves exactly as today. This module is the only place the
  HAL knows smp.vxd exists.
- **Selection.** No new user-visible mode. Workers are a property of mode
  2 that switches itself on when the VxD is present and off when it is
  not. A `SYSTEM.INI` key to force them off is needed for measurement and
  for the first bad report.

## Work order

1. **Prove the mechanism in a throwaway probe, not in the HAL.** A Win32
   test program built the way `build-ddraw-probe.ps1` builds probes, run
   on a two-CPU guest with smp.vxd installed. It creates one thread,
   elevates it manually, flies, and from the AP writes a pattern into a
   DirectDraw-locked video-memory surface and into a system-memory buffer,
   then lands and reports `cpuindex()` before and after. If the aperture
   write from the AP faults back to the BSP on every store, or the thread
   never leaves the BSP, stop here and write the decision record. Nothing
   else is worth building without this.
2. **Measure the two costs the design assumes.** In the same probe: the
   round-trip of a generation hand-off and join between BSP and AP, in
   TSC ticks and in microseconds; and the cost to the BSP of one
   placeholder thread spinning in `Sleep(0)` for a second. These two
   numbers set the batch threshold and say whether a resident worker is
   acceptable at all.
3. **Banded rasterizer, host-tested.** The stride and phase parameter and
   the union test. This step has no dependency on smp.vxd and is safe to
   land on its own; it is also the only part of the plan that can be held
   to a pixel table.
4. **The client module and the worker loop, behind a build flag.** Wired
   into the software engine with the threshold from step 2, off by
   default until step 5 passes.
5. **Measure on the two-CPU guest.** The parent plan's mode 2 instrument
   ([`dispbench-as-the-measurement-instrument.md`](dispbench-as-the-measurement-instrument.md))
   with workers forced off and on, into a video-memory target and, once
   the parent's step 2 provides one, into a system-memory target. Record
   frame times, bounce counts, and how many frames drew on one CPU. The
   decision record states plainly that the numbers come from an emulator.
6. **Decide.** If the gain into the system-memory target is not
   comfortably above the hand-off cost, the code stays behind its flag and
   the record says why. If it is, the flag flips to auto-detect and the
   settings page grows a line saying how many CPUs the rasterizer found.

Steps 1 and 2 are a day. Step 3 is a day. Step 4 is two to four days.
Steps 5 and 6 depend on the parent plan and on having a guest that
smp.vxd boots on, which the README says has not been tried on QEMU.

## Suggestions for the smp.vxd author

Things that would make this integration simpler or safer. Each is stated
against the code at `eeca7fa`, with what this driver would do with it.
None is a request to change the model.

1. **Let the caller learn whether `fly()` took.** `switch_to_ap` returns
   0 when no AP is free or the thread is not elevated
   (`taskswitch.c:395`, `:489`), and `callback_int3` discards that
   (`taskswitch.c:546`). The user-mode `fly()` returns nothing. A return
   value in EAX, or a per-thread status readable through the
   `SMP_OFFSET_THREADDATA` block, would let a worker know it is still on
   the BSP without executing CPUID. This driver would branch on it at the
   top of every work item.
2. **Make the autorun range configurable, or lift it.** The
   `0x00100000-0x7FFFFFFF` test at `taskswitch.c:156` excludes every DLL
   in the shared arena, which is where a Windows 9x display HAL has to
   live. A per-thread opt-in at elevate time, or a `[smp]` key, would let
   shared-arena code use autorun. Manual mode works around it, but every
   bounce then needs an explicit re-fly.
3. **Count bounces per thread and expose the count.** `callback_pf` sets
   `ts->pagefault = 1` (`taskswitch.c:614`) and the exception path lands
   the thread, but nothing counts how often. A counter in the thread data
   block, readable from user mode, is the one instrument a renderer needs
   to tell "ran on two CPUs" from "ran on one and nobody noticed".
4. **Offer a no-CPUID `is_bsp`.** `is_bsp_fast` executes CPUID on every
   call (`smp9x.c:152`). CPUID is a serialising instruction and on a
   hypervisor it is a VM exit. A per-CPU page, or the CPU index already in
   the kernel page's `SMP_OFFSET_CPUINDEX` block, read through a
   segment-relative or fixed-address load, would make the check cheap
   enough to sit in an inner loop.
5. **A documented wait primitive for a worker with no work.** A worker on
   an AP can only spin; `hlt` is ring 0 and any API call bounces. A
   kernel-page entry that puts the AP into the existing `S_SLEEP` path
   until a wake from the BSP, without landing the thread, would turn a
   busy-wait into an idle. The BSP side could be an ioctl or a store the
   AP supervisor polls. Without it a resident worker pool costs a full AP
   per worker while idle.
6. **State the memory-ordering contract.** `smp_proc.c:25-57` uses
   `lock xchg` for the status word, and the placeholder lock is a plain
   store. The README does not say what a user-mode program may assume
   about visibility between BSP and AP writes. One paragraph saying
   "x86 TSO applies, use `lock` for read-modify-write, no fence needed for
   a flag store" would save every integrator the same investigation.
7. **A per-thread, not per-process, way to elevate from a DLL.** Elevate
   is tied to the calling thread through `ts_thread_tid()`
   (`smp_proc.c:62`), which is right, but the placeholder procedure and
   lock are addresses in the caller. A DLL loaded at a shared base in
   several processes supplies the same placeholder address in each, and
   whether the VxD keys any state on that address is not documented. A
   sentence confirming that the (thread, proc, lock) triple is the only
   key would close the question.
8. **Publish an ABI version.** The kernel-page offsets are fixed constants
   in `smp9x.h`. An ioctl returning a version, or a version word at a
   fixed offset in the kernel page, would let a driver refuse to fly on a
   VxD whose layout it does not know rather than jump to the wrong
   address.
9. **A Watcom-friendly client.** `smp9x.c` uses GCC inline assembly and
   `<cpuid.h>`. The ioctl and offset interface is small enough that a
   header-only client with no assembler, or with `#pragma aux` variants
   for Open Watcom, would let a project that links no C runtime use it
   as-is. This driver will write its own; upstreaming it is the obvious
   contribution back.
10. **QEMU as a tested target.** `README.md` lists VirtualBox, VMware and
    one physical board. The Windows 98 guests this project can drive
    headlessly are QEMU-based. A note on whether the AP boot sequence has
    been seen to work under QEMU's `-smp`, and with which machine type,
    would decide whether this plan's step 1 is a probe or a porting
    exercise.

## Related

- Parent plan, mode 2 section:
  [`s3-trio64-voodoo2-hybrid-3d.md`](s3-trio64-voodoo2-hybrid-3d.md)
- The scalar work that should precede this:
  [`software-rasterizer-scalar-fixes.md`](software-rasterizer-scalar-fixes.md)
- The rasterizer record:
  [`2026-09-01-software-rasterizer-depth.md`](../decisions/2026-09-01-software-rasterizer-depth.md)
- The instrument this plan measures with:
  [`dispbench-as-the-measurement-instrument.md`](dispbench-as-the-measurement-instrument.md)
- smp.vxd: `C:\everything\smp.vxd`, upstream
  https://github.com/JHRobotics/smp.vxd
