# Rendering service boundaries and workload replay

Date: 2026-11-15. Status: proposed; no implementation or validation performed.

## Objective

Make the production Direct3D command-processing path runnable on the host,
then use captured workloads to reproduce failures and measure changes without
requiring a Windows 98 machine for every iteration. Preserve the existing
chipset backends, neutral render interface and independently implemented OpenGL
driver.

This is an incremental architecture improvement. Host replay establishes facts
about command processing and emitted work; physical testing remains necessary
for pixels, GPU execution, coherency, synchronization and display behaviour.

## Current foundations and gap

The project already has useful boundaries:

- `src/display32/d3d/d3d_dp2.c` walks commands independently of Windows and
  delivers them through a caller-supplied sink.
- State, handle, mapping and other policy modules have host tests.
- `src/display32/r3d/` supplies a neutral rendering vocabulary used across APIs.
- Chipset-specific translation and hardware mechanics have separate modules.

The integration in `src/display32/d3d/d3d_core.c` still combines Windows entry
points, context management, state translation, clipping, diagnostics and access
to global HAL state. Testing the individual helpers does not exercise all of
their production interactions.

The existing DP2 ring capture records call ordering, timing and bounded command
summaries. It is useful diagnostic evidence, but is not a self-contained input
for replaying the complete command-processing path.

## 1. Introduce explicit services around command processing

Separate the Windows adapter from the processing of a command stream and its
context. Supply external operations through a small services structure with an
opaque user pointer. Determine the exact operations from existing call sites;
the initial candidates are:

- Resolve surface and texture handles into validated descriptions.
- Access bounded vertex and texture data and mip-level metadata.
- Submit neutral draws and clears, and return their actual outcome.
- Request synchronization where the existing contract requires it.
- Report diagnostics through an optional sink.

Keep Windows-owned structures, segmented pointers and runtime callbacks in the
adapter. The portable path should accept project-owned types with explicit
lengths. Keep mutable processing state associated with its context; document
any state that must remain shared across contexts.

Define ownership and lifetime for each service result, especially vertex
buffers, texture data and queued draws. Specify failure behaviour, partial
submission and invalidation after a mode change or context destruction. Do not
allow a mock service to hide missing ownership or synchronization requirements.

Start with one existing DP2 path. Use the same processing functions in the
driver and host harness; avoid a second implementation maintained only for
tests. Preserve callback return values, error offsets, draw ordering and
fallback decisions while moving the boundary.

Acceptance:

- A host test runs command parsing, state application and neutral draw creation
  together through production code.
- The portable portion includes no Windows or DDK headers and does not access
  global HAL state directly.
- Service failures and resource lifetime transitions have explicit tests.
- Existing host checks, build/audit gates and selected guest probes show no
  unintended behaviour change.

## 2. Add complete capture and deterministic replay

Build on the service boundary to capture a bounded workload and replay it
through the production core. Record enough information to reconstruct execution:

- Initial context state, relevant capabilities and configuration.
- Command bytes, referenced vertices and required resource metadata.
- Service requests, their ordered responses and submission outcomes.
- Resource creation, mutation and destruction when they affect the capture.
- Frame boundaries and relevant interleaved HAL operations.

For command-translation replay, capture the data actually consumed by that
path. Pixel replay would additionally require texture contents, initial target
contents and a suitable renderer; treat that as a separate extension.

Use a versioned format with fixed-width fields, explicit byte lengths, stable
resource identifiers and build identification. Do not serialize raw pointers
or rely on compiler structure padding. Validate all lengths and references
before consuming a capture. Mark truncation or missing data explicitly.

Capture must be opt-in and bounded in memory and disk use. Measure its overhead
on the target machine. Timing from an instrumented run must not silently become
the performance baseline for the uninstrumented driver.

Replay must fail clearly on an unrecorded service request or incompatible
capture rather than inventing a successful answer. Produce a deterministic
summary of state transitions, draw descriptions, errors and submission order.
Add backend command emission where it can run without live hardware; normalize
relocatable addresses when comparing emitted output.

Acceptance:

- A synthetic workload and a captured physical-machine workload replay with
  complete service coverage and repeatable output.
- Malformed, truncated and incompatible files are rejected with useful errors.
- A changed render state or service response produces a localized difference.
- Replay makes no hardware accesses and does not require Windows 98.

## 3. Use the execution boundary for stronger regression checks

Once the production path is callable on the host, add tests that cover its
interactions rather than duplicating isolated helper tests:

- Replay small captured workloads that exercise known failures.
- Generate bounded command/state sequences and check deterministic results,
  valid resource references and failure propagation.
- Run supported host builds under memory and undefined-behaviour sanitizers.
- Compare an optimization against a retained reference implementation or
  baseline output where exact equivalence is expected.
- Test context switching, handle reuse, target changes, queued work and
  resource destruction together.

Report CPU processing time, draw and batch counts, and emitted command volume
separately. These measurements can identify overhead or regressions; they do
not establish GPU throughput or game frame rate.

Keep a small regression corpus with the capture version, originating build,
scenario and expected result. Define tolerances explicitly where floating-point
behaviour prevents byte-for-byte comparison across host toolchains.

## Implementation order

1. Inventory the external dependencies and mutable state of one DP2 path.
   Record current outputs and failure behaviour as a baseline.
2. Extract its service boundary and run the production path in a host harness.
3. Specify the capture format and implement synthetic capture/replay first.
4. Add bounded capture on Windows 98 and replay one real workload.
5. Add differential checks, fuzzing and sanitizer coverage at that boundary.
6. Extend backend emission coverage and the regression corpus as concrete
   debugging needs arise.

Each step should be independently reviewable. Update
`docs/specifications/win9x-driver-boundaries.md` when the implemented boundary
changes. Follow the existing host, build and physical validation requirements;
record exactly which layer each result validates.

## Design constraints

Keep the neutral render interface as the shared contract for Direct3D, our own
OpenGL, Glide and 3DCIF. API-specific state belongs in its frontend, not in
private Direct3D state slots used as a cross-API protocol.

Retain per-chip backends and small portable policy modules. Split large files
where ownership becomes clearer, but judge the change by whether production
behaviour can be exercised independently, not by file size.

Do not combine the extraction with new graphics features, batching changes or
hardware optimizations. Establish equivalent behaviour first so later changes
can be evaluated against a reliable baseline.
