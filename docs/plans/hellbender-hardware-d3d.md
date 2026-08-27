# Hellbender hardware Direct3D compatibility plan

Date: 2026-08-12

Status: closed (2026-08-15) — the milestone target does not exist. Pixel
comparison against the retail S3 ViRGE driver established that Hellbender
renders in software on both drivers; no publishable capability moves it onto
hardware Direct3D (commit `beee733`). The capability and stability work done
under this plan stands and is what later titles build on.

Current implementation and VM-test status is recorded in the dated
[2026-08-13 review handoff](../handoffs/2026-08-13-hellbender-d3d-review.md).
Read and run its regression gates before reproducing the current hard wedge;
the capability list below describes the original baseline and is not the
latest implementation inventory.

Target milestone: Hellbender menus and the first mission use the Velocity9x
hardware Direct3D device on Windows 98SE with the S3 ViRGE/DX 86C375.

Product baseline: Velocity9x 0.2, Direct3D phase 3

## 1. Outcome

The milestone is complete when an installed retail copy of Microsoft
Hellbender can:

1. start without an access violation or hung desktop;
2. enumerate and select the Velocity9x hardware Direct3D device;
3. display its menus without corruption;
4. load the first mission;
5. render and accept player input in the first mission for ten continuous
   minutes; and
6. exit to an intact GDI desktop without rebooting.

This is a compatibility milestone, not a claim that all missions, effects, or
other Direct3D games work. Software rendering remains a valid recovery path,
but does not satisfy the hardware milestone.

## 2. Current boundary

Velocity9x phase 3 currently provides:

- a Direct3D HAL device and allocation-free context lifecycle;
- 16-bpp video-memory render targets;
- flat-color TL-vertex triangle lists through the legacy v1
  `RenderPrimitive` callback;
- bounded ViRGE FIFO/idle waits; and
- one pixel-verified native S3D triangle path.

It does not currently provide texture formats, texture lifecycle, Z buffering,
Gouraud shading, general render-state handling, clipping, or a validated
DirectX 2/3 execute-buffer route. Its S3D output is native ZRGB1555 while the
current Windows 16-bpp mode is RGB565.

Hellbender is a useful target because it was designed for the first Direct3D
generation and also has a software renderer. The hardware path is expected to
exercise the execute-buffer-era API, but this must be proven by tracing rather
than assumed.

## 3. Scope

### In scope

- Hellbender startup and device enumeration.
- DirectX 2/3-era HAL ABI and callback compatibility required by the game.
- One format-correct 15/16-bpp render-target strategy.
- Gouraud-shaded, textured triangle rendering.
- The texture formats and addressing modes actually requested by the game.
- A 16-bit Z-buffer if requested by the first mission.
- The render states observed during menus and the first mission.
- Fullscreen mode entry, surface loss/restoration, and return to GDI.
- Diagnostics and repeatable comparison against a stock-driver reference VM.

### Out of scope

- 3DMark99 compatibility.
- Every Hellbender mission, multiplayer, cinematics, or every optional effect.
- Bump mapping, multitexturing, anisotropic filtering, or features not used by
  the target path.
- Capabilities that the ViRGE cannot implement correctly.
- Shipping game media or Microsoft DirectX binaries in this repository.
- Performance tuning before pixel correctness and recovery are established.

## 4. Test topology

Use two independent Windows 98 VMs:

- **Reference VM:** stock S3 ViRGE/DX driver and the same Hellbender/DirectX
  installation.
- **Velocity9x VM:** current development driver and the same game data,
  resolution, color depth, audio configuration, and input settings.

Give each VM a separate writable disk, 86Box profile, serial pipe, result
directory, and host TCP forward. For example, map host ports 9869 and 9870 to
guest port 9869. Never run both VMs against the same writable disk image.

Record for both systems:

- DirectX runtime versions;
- display driver identity and build;
- enumerated Direct3D device descriptions and capabilities;
- render and Z-buffer formats;
- requested surface caps and dimensions;
- callback sequence and render states; and
- screenshots and failure checkpoints.

Reference-driver behavior is evidence about the game's requirements, not
source to copy.

## 5. Work packages

### H0 - Reproducible game baseline

Deliverables:

- Install Hellbender from user-provided media on both VMs.
- Preserve hashes of the installer/game executables and record the selected
  installation options outside release packages.
- Confirm the reference VM reaches the first mission in hardware mode.
- Confirm the Velocity9x VM can use the software renderer as a recovery path.
- Archive `HELLBEND.INI`, DirectX versions, screenshots, and launch exit codes.

Exit gate:

- The reference run is reproducible twice after cold boots, and the exact
  Velocity9x hardware failure point is reproducible without guessing.

### H1 - Startup, ABI, and capability audit

Deliverables:

- Add bounded counters or a compact trace ring for Direct3D enumeration,
  context, execute/render, texture, render-state, and surface callbacks.
- Capture the last completed callback before any `0xC0000005` failure.
- Audit every advertised capability against an implemented callback and
  pixel-verified behavior.
- Correct project-owned DirectDraw/Direct3D structure sizes, flags, calling
  conventions, and pointer indirection against the installed Windows 98 DDK.
- Return explicit unsupported results for unavailable features; never expose a
  non-null callback or capability that relies on an unvalidated layout.

Exit gate:

- Hellbender reaches its menus without crashing. Unsupported hardware paths
  fall back cleanly or produce a recorded error, and existing phase-3 probes
  remain green.

### H2 - Execute-buffer command path

Deliverables:

- Determine whether Hellbender calls HAL `Execute`, `RenderPrimitive`, or both.
- If required, implement the project-owned execute-buffer ABI and parse only
  the observed, documented instructions.
- Validate buffer offsets, instruction sizes/counts, vertex indices, and all
  arithmetic before dereferencing guest-provided data.
- Support the minimum observed instruction set, expected to include state
  changes, process-vertices/TL-vertex references, and triangle records.
- Bound work per callback and preserve the x87 state.

Exit gate:

- A synthetic probe submits the same instruction classes as Hellbender and
  produces deterministic pixels. Malformed buffers are rejected without a
  crash, hang, or out-of-bounds access.

### H3 - Correct render-target format

Deliverables:

- Resolve the current ZRGB1555/RGB565 mismatch before adding textures.
- Prefer a real 15-bpp render-target/display-mode path if that matches the
  ViRGE S3D command format and Hellbender accepts it.
- Otherwise prove and document a safe RGB565 S3D command configuration; do not
  silently reinterpret 1555 output as 565.
- Update mode/format enumeration, surface validation, fill/readback helpers,
  and diagnostics for the selected strategy.

Exit gate:

- Red, green, blue, white, black, and mixed-color triangle samples match a
  software reference at multiple locations and pitches. GDI and DirectDraw
  restoration pass after hardware rendering.

### H4 - Gouraud shading and required raster states

Deliverables:

- Interpolate diffuse RGB color across TL triangles.
- Implement only observed culling, shade, fill, Z, fog, blend, and texture
  render states, with explicit defaults at context creation.
- Reset or re-establish complete S3D state when switching contexts or render
  targets; do not depend on another application's register state.
- Add degenerate, winding, clipping-boundary, and multi-triangle tests.

Exit gate:

- Synthetic Gouraud reference images pass pixel/tolerance comparison, and
  Hellbender menus render without missing geometry or persistent state from a
  previous scene.

### H5 - Texture lifecycle and sampling

Deliverables:

- Trace and implement the texture formats Hellbender actually enumerates and
  selects, initially limiting advertisement to one proven 16-bpp format.
- Implement texture create/destroy/swap/get-surface handling as required by
  the selected Direct3D ABI.
- Validate video-memory offsets, dimensions, pitch, alignment, and power-of-two
  constraints before programming S3D.
- Implement perspective-correct texture coordinates and the observed
  addressing/filtering modes, starting with nearest filtering and wrap/clamp.
- Handle texture replacement and surface loss without stale hardware handles.

Exit gate:

- Checkerboard, UV orientation, wrap/clamp, perspective, and texture-lifetime
  probes pass. Hellbender menu and cockpit textures render coherently.

### H6 - Z-buffer and first-mission integration

Deliverables:

- Enumerate and create the minimum observed Z-buffer format, expected to be
  16-bit.
- Implement Z clear, compare, enable/disable, and write-enable behavior used
  by the game.
- Validate Z base, pitch, dimensions, alignment, and bounds independently of
  the color target.
- Exercise render-target/Z-buffer switching and repeated mission loads.
- Add timeout recovery that returns to an operable desktop even if S3D stops
  making progress.

Exit gate:

- An overlapping-geometry probe passes all supported Z comparisons, and the
  first mission renders stable depth ordering for ten minutes.

### H7 - Acceptance and hardening

Run, in this order:

1. cold boot and unattended GDI test;
2. existing DirectDraw and phase-3 Direct3D probes;
3. Hellbender launch, menus, first-mission load, and ten minutes of input;
4. exit to desktop and rerun GDI/DirectDraw probes; and
5. warm reboot followed by another launch/mission/exit cycle.

Repeat the complete sequence on at least five cold boots. Preserve result INIs,
driver trace summaries, screenshots, and the exact Velocity9x build ID.

Exit gate:

- Five of five complete cycles pass with no crash, hang, unexplained fallback,
  leaked context/texture handle, corrupted desktop, or required reboot.

## 6. Capability policy

Capability advertisement follows implementation, never planned work:

- Add one flag or format at a time.
- Require a synthetic pixel/lifecycle test before exposing it to the runtime.
- Keep unsupported states unadvertised and reject unexpected calls safely.
- Do not broaden caps merely to get past Hellbender startup.
- Record every new advertised bit and its validating probe in the decision
  document for the accepted build.

## 7. Diagnostics

Extend machine-readable results with:

- device-enumeration and selection outcome;
- callback call counts and last callback/opcode;
- last render state and unsupported state ID;
- context, texture, render-target, and Z-buffer handles;
- target/texture/Z offsets, pitch, dimensions, and formats;
- engine timeout/reset counts;
- Hellbender process exit code and elapsed mission time; and
- whether rendering used HAL hardware or software fallback.

Tracing must be allocation-free in HAL callbacks, bounded, and disabled or
cheap in non-diagnostic builds.

## 8. Safety and recovery

- Take a cold copy of each VM disk/configuration/NVR set before driver updates.
- Keep the reference VM immutable except for documented game configuration.
- Stage locked driver files through the existing verified update procedure.
- Require byte comparison after reboot.
- Never let a failed hardware callback spin indefinitely; use bounded waits
  and retain standard-VGA recovery instructions.
- After a crash, capture the HAL trace, agent log, serial tail, screenshot, and
  result files before rebooting.

## 9. Estimated effort and decision points

Expected effort for the stated milestone is two to four focused weeks. Stop and
re-scope at these decision points:

- **After H1:** if the game still crashes before any documented HAL call,
  investigate runtime/game compatibility separately before adding raster
  features.
- **After H3:** if format-correct ViRGE rendering cannot coexist with a mode
  Hellbender accepts, do not continue to textures under a false format claim.
- **After H5:** if required texture behavior exceeds documented ViRGE hardware,
  choose an explicit software fallback rather than advertise incorrect caps.
- **After H6:** if the first mission requires substantial unplanned features,
  record them and create a follow-on milestone instead of silently expanding
  this one.

## 10. Definition of done

“Menus and first mission use hardware D3D” means all of the following are true:

- the selected device is the Velocity9x hardware HAL;
- the game is not using HEL/software rasterization for the accepted path;
- menus and first-mission color, textures, and depth are visually coherent;
- input remains responsive for ten minutes;
- the game exits normally;
- the desktop and existing regression probes remain healthy;
- unsupported capabilities remain unadvertised; and
- the accepted build, evidence, limitations, and recovery procedure are
  documented in the repository.
