# HD 4600, 3DMark 2001 SE: told the driver draws DOTPRODUCT3, the DOT3 Bump Mapping test faults in 3DMark's own renderer

Date: 2026-11-14. Seen on the i5-4570 (HD 4600, W9QI5A83), boots 247 and
248, intel-gma set built from `9c7445ec` plus the DOT3 and environment
bump change, dirty. 3DMark 2001 SE build 330, 1024x768, 32-bit colour,
compressed textures, D3D Software T&L, DirectX 9.0c. Evidence:
[`../probe/hsw-dot3-embm-2026-11-14/`](../probe/hsw-dot3-embm-2026-11-14/).

Status: **closed 2026-11-14: not the driver.** The fault happened only
while a d3d8 logging proxy of ours sat in 3DMark's folders; it shifted
the stack so that an array entry 3DMark never initialises read as
"transformed". Without the proxy the test runs, and the driver claims
`D3DTEXOPCAPS_DOTPRODUCT3` (last section). The sections between are
the record of getting there, wrong turns included.

## What happens

With `D3DTEXOPCAPS_DOTPRODUCT3` in the DDI 8 caps, a run of the "Bump
Mapping" pair (Select Tests, Clear, Bump Mapping) draws Environment Bump
Mapping, makes a second device for DOT3 Bump Mapping, and the process
ends there with no dialog. Two runs of two, the same address both times.

The HAL's fault watch (`V9XTRACE-boot247-dot3-fault.INI`,
`V9XTRACE-boot248-dot3-fault.INI`):

- `FaultCode=0xC0000005`, `FaultAddress=0x01FBB730`, module base
  `0x01FA0000`.
- The stack's return addresses are all in that module (`01FAD80B`,
  `01FADF3E`, `01FAA4B8`, `01FB6237`); none is the HAL's, which sits at
  `B04xxxxx`.
- `Edx=0x00000080`, `Edi=0x00000002`.
- The last HAL call in the trace ring is a `D3dTargetLayout` enter, not
  a draw. `Ps11KernelsBuilt` was 13 before 3DMark started and 13 after
  the fault on boot 247: no DOT3 draw had reached the engine's shader
  path.

## Which module, and what it was doing

The module is `e2driver\e2_d3d8_driver_mfc.dll`, 3DMark's Direct3D 8
renderer: it is the only DLL of 3DMark's whose bytes have `call`
instructions ending at all four return offsets (D80B, DF3E, A4B8,
16237), and the call at D806 targets 1B730, the fault address. Not
`D3DR.DLL` (the Direct3D 8 runtime behind the logging proxy), not
`e2mfc.dll`, not `rlmfc.dll`.

Read from the bytes by hand, with the import names from the DLL's import
table (no disassembler was used, so this is a reading, short enough to
check):

- The function at D710 loops over texture coordinate sets 0 to 7 and
  maps each to `D3DTS_TEXTURE0` + n (the constants 10h to 17h).
- For a set whose map `P_MaterialInfo::usesTextureTransformation(n)`
  says is transformed, it calls
  `P_VirtualObject::getPrimitiveInstance`, then
  `P_PrimitiveInstance::getTextureMatrix(n)`, copies the matrix to a
  static (the copy is the function at 1B730) and calls the device's
  `SetTransform`.
- `getTextureMatrix` in `e2mfc.dll` is `[this+18h] + n * 64`: element n
  of an array of 4x4 matrices. With `Edx` 80h the array pointer was null
  and n was 2, which `Edi` agrees with.

So the material says its map 2 has a texture transformation, and the
object being drawn has no texture matrices at all. The copy reads
address 80h.

## What 3DMark asked the driver first

The d3d8 proxy's log for the run
(`d3d8-proxy-bump-tests-boot247.txt`), stage states in hex:

- DOT3 test, device 2: `ValidateDevice` answered one pass for stage 0
  `DOTPRODUCT3(TEXTURE, DIFFUSE)`, alpha `MODULATE`, coordinate set 1,
  with stage 1 `MODULATE(CURRENT, TEXTURE)` on coordinate set 2; and for
  the same with `SPECULAR` in place of `DIFFUSE`.
- The log's last lines are ordinary one-stage draws. It is buffered, so
  it does not show the call the fault came in.

Stage 1 reading coordinate set 2 and the faulting index being 2 agree;
that is an observation, not a cause.

## Not established

- Why the object has no texture matrices. Nothing was read of who calls
  `P_PrimitiveInstance::setTextureMatrix`, or of what in the caps decides
  the material 3DMark picks.
- Whether a caps value of ours leads 3DMark there: four blend stages and
  four simultaneous textures with software vertex processing is a
  combination its 2001 hardware may not have had. Not tested.
- Whether the same happens on any other driver in software T&L. No
  reference card has been run.
- Whether the HAL's DOT3 draws for 3DMark's stage setup are right. They
  never ran. V9XD32's `FfDot3` and `FfDot3F` rows pass on the same boots,
  with other arguments and one stage.

## 2026-11-14, later: three hypotheses the evidence killed

Measurement boots 250 to 254, each the whole set with `DOTPRODUCT3`
claimed again and a temporary log of texture-stage states in the HAL
(not kept in the tree). Every one faulted at the same address with
`Edx` 80h and `Edi` 2.

- **The driver draws something wrong first.** Boot 252's log, armed by
  3DMark's first `DOTPRODUCT3` on its 1024-wide device
  (`m-tss-boot252-3dmark-dot3-device.txt`, 658 lines: stage, state,
  value): the DOT3 states appear once, in the `ValidateDevice` questions
  at load, and are put back. What follows is about seventy frames of
  one-stage draws on stage 0 (the loading screen), then the textures
  unbound. No DOT3 state is set again. 3DMark faults setting up its
  first DOT3 draw, before a state of it reaches the driver.
- **Four blend stages and four textures.** Boot 253 claimed two of each:
  the same fault (`V9XTRACE-boot253-dot3-fault-two-stages.INI`). T19
  fails there by design, its programs wanting four.
- **No `D3DPTEXTURECAPS_PROJECTED`.** Boot 254 claimed it: the same
  fault. (The fault context of boot 254 was not fetched; the process
  ended at the same point in the run, 25 s in, which is all that is
  known of it.)

## What the map is, from 3DMark's code

Read with `dumpbin /disasm` from `3DMark2001SE.exe` and the two DLLs;
import names are the binaries' own. A reading, not a measurement.

- The executable never enables a texture transformation on a fixed map
  2. Four of its five `P_Material::enableTextureTransformation` calls
  pass map 0. The fifth (near 587BCC) is generic: for a map it is given
  it sets texture coordinate source 4, enables transformation and
  projection, and stores a matrix built on the half-scale, half-offset
  bias (0.5, -0.5, 0.5, 0.5) in the scene object. That is a projected
  texture, which fits stage 1 of the DOT3 material: a 16 x 16 map
  multiplied in on coordinate set 2.
- The stored matrices reach a primitive instance through
  `P_PrimitiveInstance::setTextureMatrix` in a per-object update loop
  (near 577881), which runs only when a byte flag in the scene object
  (offset 106h) is set. Nine places set that flag; what they stand for
  was not read.
- The renderer takes the path that reads the instance's matrix when a
  virtual call on its primitive (vtable slot 48h) answers true. By its
  place beside `supportsTextureCoordinateGeneration` in `e2mfc.dll`'s
  exports this is probably `supportsTextureTransformation`; the
  renderer's own implementation was not found.

So the fault is a projected light map whose matrix was never copied to
the object being drawn. Whether that is the dirty flag never being set
on this path, or the renderer taking a path that real cards do not, is
not established. Nothing found so far depends on a value the driver
reports.

## 2026-11-14, evening: six more things it does not depend on

Boot 255, the tree's own build (no `DOTPRODUCT3` claimed). The d3d8
logging proxy in 3DMark's folders was extended so a control file
changes what 3DMark is told, with no driver change and no restart: a
caps word patched, a `ValidateDevice` answer refused, a device made
with software vertex processing whatever was asked. One run a minute.
The control run with no patch completes.

Every run below had `DOTPRODUCT3` added to TextureOpCaps by the proxy,
and every one ended as before: all seven fault records have the same
offset B730 in the renderer (the module sat at 01F80000 on this boot)
with `Edx` 80h and `Edi` 2. Only the last record is kept here.

| Also told | Outcome |
|---|---|
| nothing else | died |
| every texture op (TextureOpCaps 03FEFFFFh) | died |
| no pixel shaders (version 0) | died |
| no vertex shaders (version 0, no constants) | died |
| `ValidateDevice` refuses DOT3 with SPECULAR | died |
| `ValidateDevice` refuses DOT3 with DIFFUSE | died |
| hardware T&L: DevCaps bit, lights, clip planes; 3DMark then runs as "D3D Hardware T&L" and asks for mixed vertex processing, which the proxy makes software | died (`V9XTRACE-boot255-dot3-fault-hardware-tl-mode.INI`) |

Refused, 3DMark asks the same question three more times and goes on to
draw anyway. In the hardware T&L run the proxy logged the bump test's
own `SetTransform(TEXTURE0)` with coordinate index 30000h and transform
flags 103h, which works; the DOT3 device never reaches a `SetTransform`
(`d3d8-proxy-boot255-hardware-tl-mode-tail.txt`).

With the driver-side boots that makes nine variations of what 3DMark
is told, none of which moves the fault.

## What Ghidra shows (decompiled, names are the binaries' own)

The earlier reading of flag 106h as a "dirty" flag was wrong: it says
the scene object has a material list, and the loop is the object's
construction, not a per-frame update.

- **The renderer.** `FUN_1000d860` in `e2_d3d8_driver_mfc.dll` draws an
  object through Direct3D's fixed-function vertex pipeline: world, view
  and projection by `SetTransform`, lights, fog. It calls the faulting
  function when `P_D3DIndexedTriangleList::usesD3DTexturePipeline()` is
  true, a bit the engine sets per primitive through `setProcessing`.
  The faulting function is as read before: for each texture coordinate
  of the pass whose map `usesTextureTransformation`, it takes
  `getTextureMatrix(n)` from the primitive instance with no check.
  The engine's own CPU path (`FUN_10038070` in `e2mfc.dll`) reads it
  the same way, also unchecked.
- **Where the matrix comes from.** The executable's material builder
  (`FUN_00587dc0`) adds layers in order: the colour map, a bump layer
  when the material's bump type asks, then a projected light map
  (`FUN_005878d0`): coordinate source 4, transformation and projection
  on, and the matrix kept in the material's wrapper. With colour and
  bump layers present the light map is coordinate 2, which is the
  faulting index. Whether the light map uses a matrix at all is a mode
  read from the scene file (`FUN_00592d50`), not from caps.
- **How an object gets it.** When an object is built (`FUN_005771a0`)
  each primitive instance is given its material and every matrix the
  material's wrapper keeps. `P_PrimitiveInstance::copyContents` copies
  the matrices when an object is cloned.
- **Where it can go missing.** `FUN_005668a0` clones an object from a
  model and then calls `FUN_0057d430`, which replaces each instance's
  material with the scene's material of the same name. It calls
  `setMaterial` and nothing else. An instance whose model material kept
  no matrix, given a scene material with a transformed light map, is
  exactly the fault: the matrix list was never allocated (its pointer
  was null, not merely short).

That is a defect in 3DMark's scene set-up, and nothing on the path read
so far asks the driver anything. It does not prove every card faults:
what decides the model's material and the scene's material for the DOT3
scene was not followed to its end, and one of them may yet depend on
the hardware. No card other than this one has been run.

## Next, when resumed

Superseded by the section below.

## 2026-11-14, night: the cause, and it was our own logging proxy

**Closed.** The fault needs three things together, and the third was
ours: a d3d8.dll logging proxy that earlier sessions left in 3DMark's
two folders. With the proxy renamed away, the DOT3 test runs on a build
that claims `DOTPRODUCT3` and changes nothing else (boot 258, two runs
of two). The driver was never at fault and needs no workaround.

Most of the reading in the sections above was of the wrong code: the
scene builder with the projected light map serves the game tests, not
this one. What follows replaces it.

### The three things

1. **3DMark does not initialise an array entry.** Its DOT3 test builds
   its material by hand (`FUN_004e6380` in the executable; the strings
   "Dot 3 pass 0 failed" to "pass 2" are its). `P_Material::init(4, 3)`:
   four textures, three coordinate sets. It then fills three arrays of
   per-coordinate settings, and the code writes entry 0 and then entry 1
   twice:

       004E7192  mov [ebp-254h],0   004E719C  mov [ebp-20Ch],0   004E71A6  mov [ebp-44h],2
       004E71AD  mov [ebp-250h],0   004E71B7  mov [ebp-208h],0   004E71C1  mov [ebp-40h],3
       004E71C8  mov [ebp-250h],0   004E71D2  mov [ebp-208h],0   004E71DC  mov [ebp-40h],2

   Entry 2 of each is never written. The renderer's
   `P_D3DMaterialInfo::endValidation` reads eight entries of each, and
   an entry of the first array that is 2 or 3 (with or without 100h)
   sets "this coordinate set is transformed" in the material.
2. **The slot holds what the last call left there.** Entry 2 of that
   array is the stack address 00B6F298 on this machine, run after run.
   The last thing to write it before the test's set-up is the Direct3D
   8 runtime creating a vertex buffer: its create routine (D3DR.DLL,
   the runtime the proxy loads, at 4574A0) saves `esi`, which holds the
   buffer's pool, in its prologue. 3DMark creates its loading-screen
   buffer there with `D3DPOOL_SYSTEMMEM`, pool **2**
   (`CreateVertexBuffer 60 usage 220 fvf 44 pool 2`).
3. **The proxy moved the stack by its own frame.** Its
   `CreateVertexBuffer` wrapper sits between 3DMark and the runtime, so
   the runtime's saved register lands one wrapper frame lower than it
   would: exactly on the slot. Without the proxy something else is
   there, and the test runs.

So the material said coordinate set 2 was transformed, the renderer
asked the object for texture matrix 2, no object has one, and it copied
from address 80h.

### How it was measured (boots 255 to 258)

The proxy was extended to hook the executable's and the renderer's
import tables (its own data; an attempt at patching the engine's code
leaked into another mapping of the DLL on Windows 98 and was dropped),
to read and write the test's stack frame at its `getDeviceMaterial`
call, and to sample one stack address at every logged call.

| Run | What | Outcome |
|---|---|---|
| m1 | the renderer's `getTextureMatrix` import answers an identity matrix where the object has none | completes; eight objects, one material, all without a matrix |
| p1 | the material's flags logged at each set-up call | transformation word 0 through `getDeviceMaterial`, 4 at the draw |
| d1 | the test's arrays read at that call | entry 2 of the first is 2, beside a return address into the runtime |
| d2 | that entry cleared, nothing else | **completes** |
| w1, vb2 | the slot sampled at every call | becomes 2 at `CreateVertexBuffer ... pool 2` |
| tlv1 | 3DMark told `D3DDEVCAPS_TLVERTEXVIDEOMEMORY`: it asks pool 0 | completes; the slot holds 44h, the buffer's FVF |
| boot 257 | a build claiming that bit and `D3DDEVCAPS_HWVERTEXBUFFER`, proxy passive | completes |
| boot 257 | the same, **proxy renamed away** | completes, twice |
| boot 258 | a build with neither bit, `DOTPRODUCT3` claimed, **no proxy** | **completes, twice** |

The last row is the control that should have been run first. The two
DevCaps bits are not in the tree: they changed what every DirectX 8
program is told about vertex buffers to work around our own tool.

### What is kept

- The proxies are renamed `D3D8.PRX` in both 3DMark folders on the
  4570 and are not to go back except for a measurement, and then out
  again. A proxy changes the stack below every call it wraps, and it
  cost a third of the frame rate (Environment Bump Mapping 29 fps with
  it, 48 without).
- Every 3DMark 2001 SE figure taken on the 4570 while a proxy was in
  (the machine notes have one active by 2026-11-12) was taken through
  it. The records that quote one
  say which build's proxy was there; none was compared with another.
- Killed along the way, each by a run: stage count, projected
  textures, every texture op, no pixel or vertex shaders, either
  `ValidateDevice` answer, hardware T&L mode, the bump test's leftovers
  (the DOT3 test alone faulted too), and the proxy's own
  `ValidateDevice` read-back.
