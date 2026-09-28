# ATI Rage Mobility-M hardware Direct3D and OpenGL

Date: 2026-09-27

Status: in progress — Phase 0 is register-valid. The measured memory-type code
6 is accepted as authoritative for this Gateway board by explicit operator
direction; Phase 1 may proceed using the SGRAM decode while keeping block write
disabled until separately tested.

## Progress — 2026-09-27

The standalone `ATIMM.EXE` + `ATIMM.VXD` Phase 0 probe was built and run on the
Gateway Solo 2150 through the remote agent at `10.0.1.22:9869`. It made no
engine, framebuffer, scratch, or PCI configuration writes. The LCD selector
path remained gated off because chip identity could not yet be established.

The first capture established:

- PCI identity `1002:4C4D`, revision `64`, subsystem `107B:2150`, at bus 1,
  device 0, function 0;
- the stock-driver session reads raw PCI BAR0/BAR2 as zero and command as
  `0080`, while Windows Config Manager retains the expected allocations BAR0
  `F5000000` and BAR2 `F4100000`;
- read-only attempts through assigned BAR2 and the documented BAR0
  in-aperture register page both returned zero, so neither was accepted as a
  valid register window;
- the probe returned `REVIEW` without attempting the indexed LCD reads or any
  wider snapshot after `CONFIG_CHIP_ID` failed to match.

After an explicitly authorized tier-0 driver-binding attempt and one reboot,
Windows retained the stock ATI display driver but restored live PCI command
and BAR values. The agent returned with the matching pending-job token and a
ready 1024x768x16 desktop. Two complete probe executions then produced
byte-identical reports (`CRC32 448C104B`) with:

- command bits `0x7`, BAR0 `F5000000`, BAR2 `F4100000`, and a validated
  `CONFIG_CHIP_ID` of `64004C4D`;
- 4 MiB VRAM, 1024-pixel active pitch at offset zero, and stable 1024x768 LCD
  size fields;
- stable repeated MMIO reads with no unexpected deltas and a restored LCD
  selector;
- `CONFIG_STAT0 = 00C00096`, hence `CFG_MEM_TYPE_T = 6`, not the audited
  expectation of 4.

The 264xT decode table identifies code 6 as 32-bit SGRAM at 2:1. That conflicts
with the generic `ATI MACH64 SDRAM BIOS 4.216` string used to infer code 4.
The initial probe correctly returned `REVIEW`. On explicit operator direction,
the physical measurement now supersedes the BIOS-string inference for this
subsystem and revision: the Phase 0 validator expects code 6. Engine work may
continue, while block write remains disabled until it has its own bounded test.
The durable initial capture and run notes are in
[`../probe/ati-rage-mobility-m-phase0-2026-09-27/`](../probe/ati-rage-mobility-m-phase0-2026-09-27/).

After the measured code 6 was accepted for this exact board, probe build
`ati-p0-20260927-f` was deployed. The laptop's power-managed display first had
to be woken; while asleep, PCI command/BAR reads returned their disabled
values. In the active desktop state the probe returned `PASS` with BAR2
`F4100000`, the same identity/panel/VRAM values, and
`MemoryTypeName=SGRAM-2to1-32bit` (`CRC32 DC624323`).

Phase 1 has started with a project-owned register subset and a host-testable
shared engine core in `ati_mach64_engine.h` / `mach64_engine.c`. It currently
provides:

- pre-VTB `FIFO_STAT` population-count and VTB+ `GUI_STAT[25:16]` free-count
  decoding;
- a cached exact-batch reservation path with no status read between the
  reserved writes;
- bounded FIFO and idle waits with timeout/quarantine accounting;
- the host-error mask/ack, bus flush, active-low GUI reset, full shadow replay,
  and post-replay idle sequence;
- the engine-to-CPU drain plus `INVALIDATE_RB_CACHE` boundary.

Fake-MMIO host tests cover both FIFO models, batch accounting, timeout bounds,
the no-inner-read transcript, reset ordering, replay, and cache invalidation.
The code is not yet selected by the shipping HAL and publishes no capability.

The Phase 1 core is compiled into `V9XHAL.DLL` behind the new
`V9X_DD_ENGINE_TYPE_ATI_MACH64` selector. The ATI manifest still publishes
`NONE`, and the fill/copy entries deliberately decline, so this adds no public
acceleration yet.

The first physical write diagnostic, `ati-p1-20260927-a`, then performed a
strictly gated `SCRATCH_REG0` write/read/restore transaction. It ran twice with
byte-identical `PASS` reports (`CRC32 6CF18CD0`): original `04100400`, pattern
readback `55555555`, restored `04100400`. The durable report is in
[`../probe/ati-rage-mobility-m-phase1-2026-09-27/`](../probe/ati-rage-mobility-m-phase1-2026-09-27/).

Phase 2 has started. Build `ati-p2-20260927-a` submitted one guarded 16x16
RGB565 solid fill into a backed-up 4 KiB page at VRAM offset `00200000`, then
used the shared idle/cache-invalidation boundary for CPU verification. Two
runs produced byte-identical `PASS` reports (`CRC32 B11C537F`) with zero target,
guard, and restoration mismatches. The diagnostic restored ten non-trigger
engine registers, `MEM_BUF_CNTL`, and the complete target page. Evidence is in
[`../probe/ati-rage-mobility-m-phase2-fill-2026-09-27/`](../probe/ati-rage-mobility-m-phase2-fill-2026-09-27/).

The exact 12-write fill stream is now generated by the shared Mach64 builder,
host-tested against the physical transcript, and used by the dormant HAL
Mach64 fill entry. Trigger registers are explicitly excluded from the recovery
shadow so reset replay cannot accidentally repeat an old draw. ATI still
publishes engine type `NONE`; this does not yet expose acceleration.

Build `ati-p2-20260927-b` extended that same bounded scene to 1,000 fills,
alternating RGB565 magenta and green with an exact two-slot reservation before
each colour/trigger pair. It completed in the guest with zero target, guard,
or restoration mismatches (`CRC32 5954CD4E`), leaving the display and remote
agent stable. This closes the single-boot solid-fill stress check; the second
cold-boot repetition and the remaining Phase 2 clear/coherence operations are
still outstanding.

The shared core now also generates a complete 14-write RGB565 screen-copy
stream, including source/destination offset-pitch, scissors, copy datapath,
overlap direction, and the final trigger. Host tests cover all four direction
encodings and both coordinate/VRAM bounds. Physical build
`ati-p2-copy-20260927-a` matched a CPU memmove reference for four overlapping
16x8 copies, one in each X/Y direction combination, with zero whole-page or
restoration mismatches. Build `ati-p2-copy-20260927-b` then cycled those cases
for 1,000 copies with a mandatory full-idle wait after every trigger. It
completed in 689 ms with all mismatch counters at zero (`CRC32 8A544F11`) and
left the display and remote agent stable. Evidence is in
[`../probe/ati-rage-mobility-m-phase2-copy-2026-09-27/`](../probe/ati-rage-mobility-m-phase2-copy-2026-09-27/).

The ATI manifest still publishes engine type `NONE`; neither proven primitive
is public while the rest of the Phase 2 gate and cold-boot repetition remain
open.

Build `ati-p2-clear-20260927-a` reused the proven fill stream to clear a
guarded 32x16 RGB565 color surface to `0000` and a correctly sized/pitched Z16
surface to `FFFF`. Both clears were followed by full idle, read-cache
invalidation, CPU verification, and exact restoration. The combined
1,002-operation report passed with zero interior, guard, or restoration
mismatches (`CRC32 C65EC052`). This closes the private single-boot color-clear,
Z16-clear, and engine-write-to-CPU-read mechanics. Copy-based front-buffer
presentation, target-mode coverage, and cold-boot repetition remain open.

Build `ati-p2-present-20260927-b` then exercised copy-based presentation from
the 2 MiB offscreen page into the live 1024x768x16 scanout. It copied a 16x2
rectangle across different 128-byte source and 2048-byte destination pitches,
waited fully idle, invalidated the read cache, verified the 32 destination
pixels plus all 2,016 untouched words in the two mapped scan lines, and
restored the complete 4 KiB front-buffer backup. Presentation and restoration
mismatch counts were zero (`CRC32 BAA3520D`), and the display and agent stayed
healthy. This closes the single-boot operation list at the current mode;
640x480x16 and 800x600x16 repetitions plus cold-boot coverage remain open.

The presentation diagnostic now derives the destination offset/pitch from the
live `CRTC_OFF_PITCH`. The full 1,001-copy sequence passed after live switches
to 640x480x16 (1,280-byte pitch) and 800x600x16 (1,600-byte pitch), with zero
overlap, presentation, or restoration mismatches in both modes. The desktop
was restored to 1024x768x16. Copy/presentation target-mode coverage is closed;
cold-boot repetition remains open.

The fill/clear mode repetition exposed one still-open defect at 640x480x16:
the guarded full-surface color clear reproducibly leaves logical pixel `(0,0)`
at the sentinel value while every other target pixel, every guard, and the
restored page match. Reversing X direction did not change it, and an explicit
host-write readback reduced but did not eliminate the observation. The
offscreen `(8,8)` fill and all copy cases still pass. Acceleration therefore
remains unpublished until the origin behavior has a proven workaround rather
than being hidden by the otherwise successful mode matrix.

Build `ati-p2-origin-repair-20260927-a` subsequently proved that workaround.
After draining the main clear, a three-write 1x1 repair aliases physical pixel
zero from an aligned base 16 bytes earlier at logical `x=8`. The complete
1,002-operation fill/color-clear/Z16-clear diagnostic passed with zero target,
guard, or restoration mismatches at 640x480x16, 800x600x16, and 1024x768x16;
the three reports were byte-identical (`CRC32 1D9A5A5C`). The shared builder
now generates this repair only when the surface has sufficient leading VRAM
and a scissor width that contains `x=8`; unsupported origin surfaces decline
to the CPU path. Phase 2's single-boot mode matrix is therefore complete.
Cold-boot repetition remains the only Phase 2 publication gate.

## Progress — 2026-09-28

Phase 3 preparation now has project-owned, host-tested builders for the exact
17-write RGB565 flat-triangle state and 19-write setup packet. The setup
builder packs X/Y in the Rage 14.2 representation, emits S/T/W, Z, ARGB and
the final `ONE_OVER_AREA` trigger, preserves its winding sign, and refuses a
degenerate triangle. Trigger writes are excluded from reset shadow replay.

Private diagnostic `ATI3D0` now passes on the Gateway at 1024x768x16. It backs
up and guards a 64x28 off-screen target, emits complete no-texture/no-depth/
no-alpha/no-fog/no-dither state, draws one triangle, drains and invalidates
before CPU inspection, records the complete intended write transcript and a
BMP, then verifies restoration of all persistent state and the full 4 KiB
VRAM page. Two same-boot repetitions were byte-identical: 256 changed pixels
bounded by `(8,6)` through `(38,21)`, zero interior/exterior/guard/restore
mismatches, no timeout, and no recovery reset (`ATI3D0.TXT` CRC32
`E28B783F`, BMP CRC32 `BD518408`). The first otherwise-correct image exposed
and led to removal of a diagnostic-only fall-through into recovery; it is
retained as negative harness evidence. A user-confirmed physical power cycle
then produced a third byte-identical report and bitmap from the immutable build
in 27 ms. The agent reported only 377,756 ms uptime before that run, consistent
with the fresh power-on, although its persistent `BootCounter` unexpectedly
remained `9`; that discrepancy is retained with the evidence rather than
silently treated as counter proof. Phase 3's two-cold-boot scene gate is now
complete. Public ATI acceleration remains disabled pending the Phase 4 feature
and publication gates.

Phase 4's first isolated feature, Gouraud color interpolation, now passes on
the same Gateway. The immutable diagnostic changed only `SETUP_CNTL` from
flat vertex-3 shading (`0x18`) to interpolation (`0x00`) and supplied red,
green and blue vertex colors. Two same-boot runs were byte-identical: the
four gradient probes passed, 256 pixels changed inside the established
`(8,6)` through `(38,21)` bounds, all guards and restored state matched, and
there was no timeout or recovery reset (`ATI4G0.TXT` CRC32 `BBA3A850`, BMP
CRC32 `C1C5460C`). A preceding invocation safely refused before MMIO or VRAM
writes when the PCI command register showed display-idle decode state `0x80`;
waking the desktop restored `0x87`, after which the unchanged build passed.
That refusal is retained as environmental evidence. Public ATI acceleration
remains disabled while the remaining Phase 4 features and publication gates
are open.

Phase 4's Z16 test-without-writes gate now also passes. The shared host core
encodes all eight Mach64 comparisons and its table-driven truth tests cover
incoming depth below, equal to, and above stored Z. Physical build
`ati-phase4-ztest-20260928-a` isolated LESS: it initialized a separately
guarded Z16 surface to `0x8000`, submitted `0x4000` at all three vertices with
`Z_MASK_EN` clear, and produced the established 256-pixel magenta triangle.
Every depth and guard word remained unchanged, all persistent engine state and
both 4 KiB VRAM pages restored exactly, and there was no timeout or reset. Two
same-boot runs were byte-identical (`ATI4Z0.TXT` CRC32 `00CA55E6`, BMP CRC32
`BD518408`) and the desktop/agent remained responsive. Evidence is in
[`../probe/ati-rage-mobility-m-phase4-ztest-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-ztest-2026-09-28/).
Z16 writes and clear remain the next Phase 4 depth gate; public ATI
acceleration is still disabled.

The Z16 write half of that gate now passes and corrected an important encoding
assumption. Enabling `Z_MASK_EN` with the historical Mesa-style `depth << 15`
setup value produced the correct color triangle but stored `0x2000` where the
API-facing value was `0x4000`, across exactly all 256 covered pixels. The
shared builder now places Z16 in setup bits `[31:16]`. Physical build
`ati-phase4-zwrite-20260928-c` then passed twice byte-identically: every
covered Z word became `0x4000`, all uncovered words remained `0x8000`, guards
and both restored pages matched, and no timeout or reset occurred
(`ATI4ZW.TXT` CRC32 `7A32B39B`, BMP CRC32 `BD518408`). The two safe REVIEW
captures and the accepted evidence are retained in
[`../probe/ati-rage-mobility-m-phase4-zwrite-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-zwrite-2026-09-28/).
The combined 3D-write/2D-depth-clear ordering diagnostic is now complete and
established another required boundary. Clearing immediately after the 3D draw
left the exact 256-pixel dirty Z triangle at `0x4000`, plus the known physical
origin word. Disabling `Z_CNTL` and waiting idle before the 2D clear retired
that dirty Z state; the existing aligned-base origin repair then produced
`0xFFFF` in every one of the 1,792 depth words. Build
`ati-phase4-zclear-20260928-c` passed twice byte-identically with zero depth,
clear, guard, state, or restoration mismatches and no timeout/reset
(`ATI4ZC.TXT` CRC32 `DAF3CFB8`, depth BMP CRC32 `D1E15135`). Negative and
accepted captures are in
[`../probe/ati-rage-mobility-m-phase4-zclear-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-zclear-2026-09-28/).
Phase 4 item 3 is closed. Public ATI acceleration remains disabled while the
remaining Phase 4 features are open.

Phase 4 item 4 has begun with a host-tested single-unit RGB565 contract:
nearest filtering, clamp S/T, replace, normalized IEEE-754 S/T, W=1, local
uncompressed storage, `TEX_CACHE_FLUSH`, and the mip-offset register selected
from `TEX_SIZE_PITCH`. The first physical build
`ati-phase4-texture-20260928-a` used an 8x8 texture at byte offset `0x204100`.
Its one bounded submission made the desktop and remote agent unresponsive;
no report or BMP became retrievable, so this is wedge evidence only and not a
rendering result. No retry or GDI capture was attempted on the wedged machine.

Review against the historical Mach64 texture heap left the 256-byte texture
base alignment as the unsupported outlier: the original driver binds local
textures at its texture-heap granularity. The shared builder now requires a
4 KiB-aligned base, and replacement build `ati-phase4-texture-20260928-b`
binds the same texture at `0x204000`; all control words, coordinates, target,
and probes are otherwise unchanged. That build crashed the freshly booted
machine in the same way, falsifying alignment as the crash cause.

Source inspection then located the failure before the first texture-state
write: texture scenes correctly left the depth page unmapped, but the shared
initializer seeded depth for every scene number greater than or equal to two,
therefore writing through the texture scene's null `AtiE6DepthLinear`. Both
crashes are diagnostic-harness failures, not texture-engine evidence. The
initializer is now bounded explicitly to depth scene modes 2 through 4. A
new state-only executable, `ATI4TS.EXE`, emits the guarded texture upload and
19 state writes but no setup vertices or draw trigger; it is the next
physical gate after a power cycle. Host tests, both diagnostic builds and the
tree check pass. Item 4 remains open and public ATI acceleration remains
disabled.

After the harness correction, build `ati-phase4-texture-20260928-c` passed
that state-only gate: 19 state writes, zero setup writes, zero changed pixels,
and exact texture, guard, engine-state and two-page VRAM restoration. The
matching draw then passed twice byte-identically. Its normalized coordinates
spanned `-0.25` through `1.25` at W=1 over an 8x8 three-quadrant RGB565
texture; exact red, green and blue probes established nearest sampling and
clamp, while all exterior and texture guards remained intact. Both runs
changed the established 256-pixel triangle bounded by `(8,6)` through
`(38,21)`, with no timeout or recovery reset (`ATI4TX.TXT` CRC32 `7C0905D2`,
BMP CRC32 `A4D8EAD8`). Evidence is in
[`../probe/ati-rage-mobility-m-phase4-texture-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-texture-2026-09-28/).
Phase 4 item 4 is closed. Perspective correction with unequal W remains the
next feature; public ATI acceleration remains disabled.

Phase 4 item 5 is closed. Build `ati-phase4-perspective-20260928-a` retained
the item 4 texture state and coordinates but submitted W values
`(1.0, 0.25, 0.25)`. Its first bounded run was safe and changed the same 256
pixels with zero exterior, texture/guard or restoration mismatches and no
timeout/reset, but two affine-era probes beside the shifted texture quadrant
boundaries reported REVIEW. Direct comparison with the W=1 BMP established
stable perspective displacement: `(30,8)` moved from green to red and
`(10,16)` from blue to red, while `(10,8)` remained red. Build
`ati-phase4-perspective-20260928-b` changed only those assertions and passed
twice byte-identically on boot 11 with `Status=0x0001FFFF`, 19 state and 19
setup writes, and zero mismatches (`ATI4PW.TXT` CRC32 `4A42453B`, BMP CRC32
`4AA1ED66`). The accepted and initial REVIEW captures are retained in
[`../probe/ati-rage-mobility-m-phase4-perspective-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-perspective-2026-09-28/).
Wrap S/T and bilinear filtering are next; public ATI acceleration remains
disabled.

The wrap half of Phase 4 item 6 now passes. Historical Mesa maps repeat by
clearing `TEXTURE_CLAMP_S/T`; build `ati-phase4-wrap-20260928-a` did exactly
that while retaining nearest filtering and the accepted W=1 scene. Its
-0.25-through-1.25 coordinates gave separate exact negative-S, negative-T,
and combined seam probes in the bottom-right white quadrant. Two same-boot
runs passed byte-identically with the established 256-pixel bounds, zero
mismatches and no timeout/reset (`ATI4WR.TXT` CRC32 `BF999F33`, BMP CRC32
`1FA18AC5`). Evidence is retained in
[`../probe/ati-rage-mobility-m-phase4-wrap-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-wrap-2026-09-28/).
Item 6 remains open for the isolated bilinear half-texel scene.

The bilinear half-texel gate also passes, closing Phase 4 item 6. Build
`ati-phase4-bilinear-20260928-a` retained clamp and set both historical
linear-filter controls (`SCALE_3D_CNTL=0x0A010081`). Constant S=T=0.5 at all
three W=1 vertices sampled the intersection of the texture's four quadrants;
three separated probes all returned mixed RGB565 `0x838E` rather than any
source texel or the sentinel. Two same-boot runs were byte-identical, with the
same 256-pixel bounds, zero mismatches and no timeout/reset (`ATI4BL.TXT`
CRC32 `F3A0455A`, BMP CRC32 `B26C4257`). Evidence is retained in
[`../probe/ati-rage-mobility-m-phase4-bilinear-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-bilinear-2026-09-28/).
ARGB1555 and ARGB4444 texture alpha are next; public ATI acceleration remains
disabled.

Phase 4 item 7 is closed. Build `ati-phase4-alpha-textures-20260928-b`
enabled texture alpha and used one `GREATER`-than-127 comparison solely to
observe transparent red versus opaque green texels. ARGB1555 and ARGB4444
each passed twice byte-identically on boot 11: transparent probes retained
the `0xA55A` sentinel, the opaque probe rendered RGB565 `0x07E0`, exactly 64
pixels changed within `(24,6)` through `(38,13)`, and all guards and restored
state matched with no timeout/reset. Their output BMPs were byte-identical
(CRC32 `7781336E`); the format-specific reports were ARGB1555 CRC32
`1E5FEE39` and ARGB4444 CRC32 `FB2A12D8`. The initial safe REVIEW, where the
diagnostic placed the alpha word in `Z_CNTL` and left `ALPHA_TST_CNTL` zero,
is retained with the accepted evidence in
[`../probe/ati-rage-mobility-m-phase4-alpha-textures-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-alpha-textures-2026-09-28/).
The complete alpha-test comparison table remains Phase 4 item 8; public ATI
acceleration remains disabled.

Phase 4 item 8 is now closed. Build `ati-phase4-alpha-table-20260928-a`
tested all eight hardware alpha comparisons independently with the proven
ARGB1555 texture, alpha populations 0 and 255, and reference 127. The exact
observed masks matched the truth table: NEVER and EQUAL changed 0 pixels;
LESS and LEQUAL changed the 192-pixel alpha-0 region; GREATER and GEQUAL
changed the 64-pixel alpha-255 region; NOTEQUAL and ALWAYS changed all 256
covered pixels. Every scene passed twice byte-identically on boot 11 with
zero interior, exterior, texture/guard and restoration mismatches and no
timeout/reset. The reports, BMPs, comparison words, CRCs and SHA-256 hashes
are retained in
[`../probe/ati-rage-mobility-m-phase4-alpha-test-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-alpha-test-2026-09-28/).
Blend ADD factor-pair validation is next; public ATI acceleration remains
disabled.

Phase 4 item 9 has begun with a side-aware shared blend encoder and the first
physical factor-pair gate. The encoder covers every source and destination
factor that has a direct Mach64 field and rejects invalid operand-side uses.
Build `ati-phase4-blend-one-one-20260928-b` then enabled ADD with ONE/ONE and
drew opaque red over RGB565 `0xA55A`. Three separated probes returned the
exact saturated sum `0xFD5A`; exactly 256 pixels changed in the established
triangle bounds, all guards and restored state matched, and no timeout/reset
occurred. Two same-boot runs were byte-identical (`ATI4B1.TXT` CRC32
`0E104B04`, BMP CRC32 `ABA09C71`). An initial safe REVIEW had the identical
correct BMP but a diagnostic range bug incorrectly ran the unmapped depth
verifier for scene 20; it is retained with the accepted evidence in
[`../probe/ati-rage-mobility-m-phase4-blend-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-blend-2026-09-28/).
Item 9 remains open for the other factor pairs selected for publication;
public ATI acceleration remains disabled.

The second item 9 gate also passes. Build
`ati-phase4-blend-alpha-20260928-a` used source alpha 128 with
SRCALPHA/INVSRCALPHA over the same destination. Its three probes returned the
CPU-predicted mixed RGB565 value `0xD2AD`; both source red and destination
green/blue remained observable. Two boot-11 runs were byte-identical, with
256 changed pixels in the established bounds, zero mismatches and no
timeout/reset (`ATI4BA.TXT` CRC32 `04B6E341`, BMP CRC32 `F954B178`). The
captures and hashes are in the item 9 evidence directory linked above. Item
9 remains open; public ATI acceleration remains disabled.

Phase 4 item 9 is closed. The advertised ADD set is sources ZERO, ONE,
SRCALPHA, INVSRCALPHA, DESTCOLOR and INVDESTCOLOR against destinations ZERO,
ONE, SRCCOLOR, INVSRCCOLOR, SRCALPHA and INVSRCALPHA, with BOTHSRCALPHA and
BOTHINVSRCALPHA as aliases. Destination-alpha factors and SRCALPHASAT are
not advertised because RGB565/XRGB1555 targets carry no alpha. Because
Direct3D publishes source and destination caps independently, build
`ati-phase4-blend-table-20260928-a` drew all 36 pairs as separate guarded
scenes with source ARGB `0xD4B16100` over `0xA55A`. Both boot-11 runs were
byte-identical PASS: every pair had zero exterior, guard, restoration,
timeout and reset counts, uniform probes and the expected changed region,
and every observed pixel matched one CPU rule (factor/255, truncating;
`ATI4BT.TXT` CRC32 `A0FB4E04`). Two other rounding rules also fit all 36, so
the rounding rule is narrowed but not settled; that matters for Phase 6's
fallback eligibility, not for publication. Texture environments (item 10)
are next; public ATI acceleration remains disabled.

Phase 4 item 10 is closed, with a semantic finding. Build
`ati-phase4-texenv-20260928-a` drew replace, modulate and alpha decal over
RGB565, ARGB1555 (alpha set and clear) and ARGB4444 uniform textures, each
unblended and through the proven SRCALPHA/INVSRCALPHA pair: 24 safe scenes
in total. Its safe REVIEW showed that MODULATE outputs texel alpha, not
At x Af. ALPHA_DECAL outputs vertex alpha. On an RGB565 texture, which has
no alpha, both the output alpha and the decal weight come from the vertex.
Build `ati-phase4-texenv-20260928-b` changed only the assertions to those
semantics, with a one-565-unit rounding tolerance that every rejected
semantic exceeds by 9 to 27 units. It passed twice byte-identically on boot
11: 21 of 24 scenes exact, all safety counters zero, and a pixel dump
identical to build a's (`ATI4TE.TXT` CRC32 `3D214DAA`). So Direct3D
MODULATEALPHA must refuse, DECALALPHA on RGB565 must emit REPLACE, and
OpenGL `GL_MODULATE` on RGBA is exact only without blending or alpha test.
Evidence is in
[`../probe/ati-rage-mobility-m-phase4-texenv-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-texenv-2026-09-28/).
Hardware scissor (item 11) is next; public ATI acceleration remains
disabled.

Phase 4 item 11 is closed. Build `ati-phase4-scissor-20260928-a` drew the
Phase 3 flat triangle under ten `SC_LEFT_RIGHT`/`SC_TOP_BOTTOM` rectangles,
encoded as the shared flat-state builder encodes them (half-open API
rectangle, inclusive registers). The rectangles were the full target,
complementary splits at x=20 and y=12, an interior box, a one-pixel column
and row, a vertex corner, and a rectangle that misses the triangle. Every
pixel of every dump matched the unscissored reference intersected with the
rectangle. Two boot-11 runs were byte-identical, with all safety counters
zero, and the reference is byte-identical to the committed Phase 3
captures. Evidence is in
[`../probe/ati-rage-mobility-m-phase4-scissor-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-scissor-2026-09-28/).
Fog without blending (item 12) is next; public ATI acceleration remains
disabled.

Phase 4 item 12 is closed for untextured draws. Build
`ati-phase4-fog-20260928-a` enabled `ALPHA_FOG_EN=2` with the
SRCALPHA/INVSRCALPHA fields and wrote the per-vertex specular alpha in one
reserved batch before the vertex batches. The 19-write setup packet does
not carry specular. All seven scenes matched `(Cv*f + Cfog*(255-f))/255`
exactly: fog off, factors 255, 0, 128, 64 and 192, and a second
`DP_FOG_CLR`. Two boot-11 runs were byte-identical with all safety
counters zero (`ATI4FG.TXT` CRC32 `D18F4F3A`). Taking the factor from
vertex alpha, inverting it, or ignoring fog each misses by at least 26
units. Only the /255 truncating rule of the four item 9 candidates fits
this data too. Textured fog, which must clear `TEX_MAP_AEN`, is not yet
measured and must refuse until it is. Evidence is in
[`../probe/ati-rage-mobility-m-phase4-fog-2026-09-28/`](../probe/ati-rage-mobility-m-phase4-fog-2026-09-28/).
Remaining Phase 4 gates: repeated texture mutation at unchanged state for
cache-flush visibility, and refusal of unsupported combinations before the
first engine write. Public ATI acceleration remains disabled.

## Goal

Add hardware Direct3D and OpenGL rendering for the physical ATI Rage
Mobility-M (`PCI 1002:4C4D`, Mach64LM) by implementing one Rage Pro-class
fixed-function backend beneath Velocity9x's shared render interface.

The destination is deliberately narrow:

- 16-bpp RGB565/XRGB1555 rendering;
- one local-VRAM texture unit;
- untextured and textured triangles;
- Z16, alpha test, additive blending, fog where it does not conflict with
  blending, scissor, flat and Gouraud shading;
- truthful Direct3D capabilities;
- the existing `V9XGL.DLL` ICD routing supported draws to the same hardware;
- whole-draw software fallback only after mixed-engine ordering and pixel
  encodings have been measured safe.

This is not a second OpenGL driver. Direct3D and OpenGL already converge on
`V9X_R3D_DRAW`; the new work is one engine implementation and the substrate
under it.

## Starting point

The `ati` family is tier-0 today:

- the VBIOS sets modes through VBE;
- the linear framebuffer is CPU-drawn;
- both ATI chips declare `EngineType = NONE` and no engine capabilities;
- no native 2D, vblank, flip or 3D operation is advertised;
- the physical Mobility-M has 4 MiB and a fixed 1024x768 panel; its live
  memory-type field reports code 6 (32-bit SGRAM at 2:1), contradicting the
  BIOS-string-derived SDRAM assumption and requiring resolution before writes;
- 86Box emulates a Mach64 VT2 2D engine, not the Mobility's Rage setup engine.

The physical identity and register-window facts are settled in
[`../decisions/2026-08-16-ati-mach64-hardware-audit.md`](../decisions/2026-08-16-ati-mach64-hardware-audit.md):

- BAR0 LFB `F5000000` on the measured laptop;
- BAR2 MMIO `F4100000`, with block 0 at `F4100400`;
- `CONFIG_CHIP_ID & 0xffff == 0x4C4D`;
- `MEM_CNTL.CTL_MEM_SIZEB` decodes to 4 MiB;
- Mobility FIFO availability is `GUI_STAT[25:16]`, not the pre-VTB
  `FIFO_STAT` occupancy bitmap;
- the part is affected by the VTB+ copy-commit and CPU-read cache hazards;
- a wedged engine can be reset with `GEN_GUI_RESETB` and full state replay.

ATI's *RAGE PRO and Derivatives Programmer's Guide* identifies Mach64LM as a
Mach64GT-core device with hardware 3D support and lists `4C4D` for Rage
Mobility M/P/M1. The *3D RAGE LT PRO Register Reference* supplies the compatible
Rage Pro-class setup, Z, alpha, texture and status register vocabulary. The
historical Mesa Mach64 DRI driver is the executable cross-check for state
translation and known fallbacks. These sources establish feasibility; only
measurements on the Gateway establish what Velocity9x may ship.

## Decisions fixed by this plan

| Question | Decision |
|---|---|
| Display ownership | Keep VBE mode setting for initial 3D. Native CRTC/panel programming is a later phase, not a first-triangle prerequisite. |
| Submission | Direct MMIO register writes with batched FIFO reservation. No AGP command DMA initially. |
| Texture memory | Local VRAM only. No AGP textures initially. |
| API architecture | One `V9X_D3D_ENGINE_OPS` implementation serves Direct3D and the neutral render interface used by OpenGL. |
| First public formats | RGB565/XRGB1555 targets, Z16, RGB565/ARGB1555/ARGB4444 textures. |
| Texture scope | One power-of-two 2D texture, nearest/bilinear, wrap/clamp, no mipmaps. |
| Unsupported state | Refuse before emission. Direct3D keeps skip-and-count; OpenGL may use whole-draw CPU fallback only after its safety gate passes. |
| VT2 | Remains non-3D. Common 2D code may serve it, but a VT2 must never acquire Mach64LM 3D caps. |
| Hardware writes | Private diagnostics first, one feature per build, on by default. No public capability bit before repeatable physical evidence. |

## Hardware capability boundary

The first engine may accept:

- flat and Gouraud shaded triangle lists;
- perspective-correct S/T using per-vertex W;
- RGB565 and XRGB1555 render targets;
- a 16-bit Z surface with all eight comparisons and independent write enable;
- alpha testing with all eight comparisons;
- blend equation ADD with supported source/destination factors;
- one RGB565, ARGB1555 or ARGB4444 power-of-two texture;
- nearest and bilinear min/mag filtering;
- wrap and clamp per axis;
- replace, modulate and alpha-decal texture environments;
- hardware scissor;
- fog only when blending is disabled.

It must initially refuse:

- stencil, accumulation and auxiliary buffers;
- mipmapping and trilinear filtering;
- the second texture input and multitexturing;
- texture borders, non-power-of-two textures, 3D/cube/rectangle textures;
- non-ADD blend equations or unsupported factor pairs;
- non-copy logic operations;
- fog plus blending;
- partial colour masks plus blending;
- 24/32-bpp render targets;
- AGP textures, VQ compression and bus-master command submission.

The silicon exposes more than this boundary. The boundary describes the first
implementation that can be made truthful and testable with 4 MiB VRAM.

## The 4 MiB constraint

A full-screen front, back and Z16 consume three 16-bpp surfaces before
texture storage and alignment:

| Mode | Full-screen front + back + Z16 | Approximate texture/scratch remainder |
|---|---:|---:|
| 640x480x16 | 1,843,200 bytes (1.76 MiB) | 2.24 MiB |
| 800x600x16 | 2,880,000 bytes (2.75 MiB) | 1.25 MiB |
| 1024x768x16 | 4,718,592 bytes (4.50 MiB) | Does not fit |

Therefore:

- 640x480 is the primary development and correctness mode;
- 800x600 may expose double-buffered Z hardware 3D with a smaller texture
  heap after alignment is measured;
- full-screen double-buffered Z at 1024x768 does not fit and must not be
  promised;
- windowed 3D on the 1024x768 desktop is the common case on a fixed
  1024x768 panel and can fit: a 640x480 window needs the 1.5 MiB front plus
  about 1.2 MiB of back and Z. It is decided by the heap, not refused by mode;
- every capability and surface-creation decision uses the current mode's
  measured heap, never the nominal 1024x1024 texture limit alone.

The remainders above are upper bounds. They do not yet subtract the 2 KiB
in-aperture register window at the top of VRAM, which stays reserved unless
`BUS_APER_REG_DIS` is set (audit §2), or any off-screen VRAM the desktop
itself holds. Phase 1 decides the first; the allocator accounts for both.

## Safety contract

Every physical-write phase follows the same contract:

1. Each build adds one bounded scene or feature, and runs it by default.
   There is no enable key; the discipline is in what changes between builds,
   and the previous build or the tier-0 package is the recovery path.
2. The diagnostic writes nothing unless chip ID, PCI revision and mode match
   the values the build was written for; a mismatch records `REVIEW`.
3. The diagnostic reserves off-screen VRAM and places guard patterns on both
   sides of every target.
4. FIFO waits have iteration and wall-clock bounds.
5. Timeout recovery performs the documented flush/reset/state replay once.
6. A second timeout, guard overwrite, display corruption or unexplained hard
   hang stops the phase; the next build may diagnose but not expand the write
   set.
7. Captures record every intended register write in order, before execution,
   so a failed boot remains reviewable.

The laptop runs on AC power with a recoverable Win98 installation. The 86Box
VT2 is not accepted as evidence for any setup-engine, texture, Z, alpha or
Mobility FIFO claim.

## Phase 0 — freeze the interface facts and add a read-only fingerprint

Implement an ATI-only diagnostic publication with no engine writes.

Read twice and decode a small allowlist:

- PCI BAR0/BAR1/BAR2 and command bits;
- `CONFIG_CHIP_ID`, `CONFIG_STAT0`, `MEM_CNTL`;
- `BUS_CNTL`, `MEM_BUF_CNTL`, `GEN_TEST_CNTL`;
- `GUI_STAT`, `FIFO_STAT`, `GUI_CNTL` where present;
- destination, scissor, datapath, Z, alpha, texture and setup control
  registers needed by the first scene;
- active CRTC offset/pitch and LCD panel-size registers, without changing
  their index state.

Do not infer the register window from a successful mapping alone. Cross-check
the PCI ID against `CONFIG_CHIP_ID` and preserve/restore the indexed LCD
selector around LCD reads.

**Artefact:** `C:\V9XDIAG\ATIMM.TXT`, containing raw reads, repeat-read deltas,
decoded identity, VRAM size, FIFO model, active target and a `PASS`/`REVIEW`
verdict.

**Done:** two cold boots produce stable identity and memory decodes; the active
surface agrees with the VBE mode; reads are neither all-zero nor all-ones; no
register changes between the two quiet snapshots except identified live
status; and the audit's three pre-flight values hold: `HORZ_PANEL_SIZE = 127`,
`VERT_PANEL_SIZE = 767` and `CFG_MEM_TYPE_T = 6` (32-bit SGRAM at 2:1 on the
measured Gateway subsystem/revision).

**Kill:** chip ID mismatch, an unexplained register-window alias, or a memory
decode contradicting the measured 4 MiB aperture. The original BIOS-derived
memory-type expectation was superseded by the repeatable live register value.

The audit's `SCRATCH_REG0` write/read probe is deliberately not part of this
phase, which writes nothing.

## Phase 1 — shared Mach64 engine substrate

Add the code that every 2D and 3D operation will use, but publish no hardware
capability.

### Register and FIFO layer

- Add the reviewed Mach64LM register and mask subset to a project-owned
  header; do not import a historical driver's definitions wholesale.
- Map block 0 from the dedicated MMIO BAR on the Mobility and the correct
  in-aperture page on VT2.
- Decide whether the Mobility sets `BUS_APER_REG_DIS`. The audit recommends
  it: it reclaims 2 KiB of VRAM and removes the chance of the engine writing
  over its own registers. Record the choice and its effect on the heap.
- Reserve an exact number of FIFO slots before a batch.
- On Mobility, cache and decrement `GUI_STAT[25:16]`; re-read only when the
  cache cannot satisfy the next batch.
- On pre-VTB VT2, retain the separate `FIFO_STAT` population-count path.
- Wait for full idle only at CPU/framebuffer, readback, mode/power and recovery
  boundaries—not between ordered register batches.

### State and recovery

- Maintain one complete software shadow of engine state.
- Skip redundant state writes only after the baseline full-state path works.
- Implement bounded idle and one reset attempt: flush, pulse the active-low
  `GEN_GUI_RESETB`, replay all state, then recheck idle.
- Record timeout, reset and replay counters in shared diagnostics.
- Route all access through the existing Win16 mutex.

### CPU coherence

- Before a CPU read after engine activity, wait and set
  `INVALIDATE_RB_CACHE` in `MEM_BUF_CNTL`.
- Define the engine-to-CPU and CPU-to-engine boundaries once; the 2D, D3D,
  OpenGL fallback and screenshot paths must use the same helpers.
- The most frequent CPU readers are DirectDraw `Lock` and GDI/DIB-engine
  reads of the screen, not the 3D paths. From the moment Phase 2 wires engine
  fill and copy into DirectDraw, every `Lock` of an engine-written surface
  and every GDI read of engine-touched VRAM crosses the same boundary
  (audit erratum E2).
- Treat the documented Mobility screen-copy commit race separately: the first
  implementation waits idle after every screen copy until measurement proves
  a weaker rule safe.

**Tests:** host tests for address arithmetic, FIFO count decoding, batch
accounting, timeout bounds, reset ordering, state replay and generation
changes. A fake-MMIO transcript test must prove that a requested N-write batch
performs no status read between those N writes.

Triangle setup arithmetic is policy and belongs here too, host-tested before
Phase 3 reaches the machine: the CPU-computed `ONE_OVER_AREA`, its sign and
the degenerate-area refusal, and the vertex encodings (fixed-point X/Y, as the
Mesa driver's 14.2 form; Z; S/T/W; colour). Tests compare against hand-worked
triangles and the Mesa driver's encoding, not against the implementation's own
output.

**Done:** the physical build fingerprints the engine and can exercise
the wait/recovery logic against injected software status without issuing a
draw command.

## Phase 2 — native 2D primitives, clears and presentation substrate

Implement conservative Mach64 operations behind private diagnostics first:

1. off-screen solid fill;
2. overlapping off-screen screen copy in all four direction combinations;
3. color clear using fill;
4. Z16 clear using a correctly sized/pitched depth surface;
5. back-to-front presentation by screen copy;
6. engine fill, then a CPU read of the same pixels through the shared
   drain/invalidate boundary, as a DirectDraw `Lock` will do.

Use `DST_OFF_PITCH`, scissors, pixel-width/datapath state and the documented
last trigger write. Do not enable block write on the SDRAM board. Enforce the
Mobility post-copy idle workaround until a separate capture supports changing
it.

**Artefact:** `ATI2D0.TXT` plus small binary/BMP dumps, containing initial and
final CRCs, sentinel values, write transcript, FIFO reads, waits and recovery
counters.

**Done:** all six operations match CPU references across 640x480x16 and
800x600x16, guards remain intact, and 1,000 alternating operations complete
without timeout or display corruption across two cold boots.

After the private gate, wire `eng_mach64.c` into DirectDraw for solid fill and
screen copy. Flip and vblank remain unadvertised unless their own measurement
exists; copy presentation is sufficient for the first 3D release.

## Phase 3 — first off-screen triangle

This is the first setup-engine write and gets a build of its own.

Use 640x480x16 with a small off-screen RGB565 target:

- no texture, depth, alpha, blend, fog or dither;
- one opaque flat-colored triangle wholly inside the target;
- complete state emitted, with no state cache optimization;
- vertices converted from the neutral screen-space representation into the
  Rage setup engine's S/T/W, color, Z and X/Y formats;
- `SETUP_CNTL` and the documented final setup write initiate exactly one
  primitive;
- CPU readback occurs only after the shared drain/coherence boundary.

The first comparison is semantic, not necessarily a full CRC: assert interior
and exterior pixels, edges, target guards and untouched neighboring surfaces.
Once the fill convention is understood, promote a stable image/hash to the
regression corpus.

**Artefact:** `ATI3D0.TXT`, recording decoded and encoded vertices, all state
and setup writes, timing, status transitions and per-pixel assertions, plus a
target dump/BMP.

**Done:** the same reviewed scene passes on two cold boots, with correct
interior/exterior probes, intact guards, no error/timeout and a repeatable edge
rule.

**Kill:** progress requires undocumented register mutation without a bounded
hypothesis, or setup writes affect memory outside the target.

## Phase 4 — useful fixed-function 3D, one feature per build

Add one independently observable feature at a time. Each step has a CPU
reference scene, its own expected transcript and no expansion after a failure.

1. Gouraud color interpolation.
2. Z16 test without writes, all eight comparisons as table-driven host tests.
3. Z16 writes and clear.
4. RGB565 texture, nearest and clamp.
5. Perspective correction using unequal W values.
6. Wrap S/T and bilinear filtering, including seam and half-texel scenes.
7. ARGB1555 and ARGB4444 texture alpha.
8. Alpha test.
9. Blend ADD with each advertised factor pair.
10. Replace, modulate and alpha-decal texture environments.
11. Hardware scissor.
12. Fog without blending.

Texture upload uses local VRAM and pulses `TEX_CACHE_FLUSH` at the defined
visibility boundary. Texture allocation records offset, pitch, format and
generation; mode change or surface loss invalidates every resident copy.

The hardware may advertise a nominal 1024x1024 limit only if allocation tests
show it can bind such a surface in a mode where enough heap exists. Otherwise
publish the largest truthful per-mode limit or decline creation from heap
availability. No silent downscale or format substitution is permitted.

**Artefact:** `ATI3D1.TXT` with one record per scene and BMP/hash output.

**Done:** every advertised state has an isolated passing physical scene;
unsupported combinations are rejected before the first command; repeated
texture mutation proves cache visibility.

## Phase 5 — Direct3D engine integration

Add `V9X_DD_ENGINE_TYPE_ATI_MACH64` and a `d3d_mach64.c` implementation of the
current engine contract:

- `limits` describes the measured target, depth, coordinate, pitch, texture
  and alignment limits;
- `texture_format` accepts only the published formats;
- `describe_caps` publishes exactly the Phase 4 feature set;
- `ready` checks the mapped, fingerprinted, non-quarantined engine;
- `accepts` is passive and rejects every unsupported combination before
  emission;
- `draw` translates `V9X_R3D_DRAW`, reserves the complete batch and emits the
  setup registers;
- surface hooks are added only if DirectDraw's normal heap cannot guarantee
  the required placement/alignment.

### Required core correction: early capability selection

`v9x_d3d_publish_engine()` currently uses software when selected, recognizes
Gen3 when its type is already present, and otherwise falls back to ViRGE. A
second shipping hardware engine cannot inherit that behavior.

The early stamp this needs already exists. `v9x_dd_stamp_engine_caps()` in
`src/display16/dd16.c` writes `engine_type` and `V9X_DD_ENGINE_VALID` from the
chip's `fill_engine_descriptor` hook before `DriverInit`, which is how Gen3 is
published. The enable ordering does not need to change. The comments above
`v9x_d3d_publish_engine()` still say `engine_type` is unreadable at publish
time; they predate that stamp and must be corrected with this work.

Before ATI exposes `V9X_DD_ENGINE_CAP_D3D`:

- implement `fill_engine_descriptor` for the Mobility (no ATI chip has one
  today) so the existing stamp carries `V9X_DD_ENGINE_TYPE_ATI_MACH64`, and
  never for VT2;
- add the Mach64 branch to both `v9x_d3d_engine()` and
  `v9x_d3d_publish_engine()`, and make them use the same selector;
- fail closed for an unknown/unavailable type rather than publishing ViRGE;
- add host tests for every engine type and for mismatched caps/type;
- prove the ATI package's texture formats and caps contain no ViRGE-only
  promises.

### D3D gates

- Extend the Direct3D probe with untextured, Z, textured, alpha-test and blend
  scenes and refusal counters.
- Exercise context creation/destruction, target changes, texture swaps,
  surface loss/restore and mode changes.
- Run 3D WinBench 98 and Final Reality only after the probe passes. First
  record that hardware draw counters increase; a visual result alone does not
  prove the hardware path ran.
- Run windowed and fullscreen at 640x480 and 800x600. Run windowed on the
  1024x768 desktop, the common case on this panel, and verify that a
  full-screen 1024x768 request refuses cleanly.

**Done:** Direct3D publishes the conservative caps, every probe scene matches
the reference within its documented raster rules, unsupported state refuses
without partial emission, and named applications demonstrably submit hardware
draws across repeated restarts.

## Phase 6 — OpenGL hardware routing and software fallback

No ATI-specific ICD is added. The existing ICD already describes batches as
`V9X_R3D_DRAW`; once the Mach64 ops table implements `draw` and `accepts`, the
same feature set can run in hardware.

### Hardware route

- Extend render-interface `describe` output for the Mach64 limits, target
  format and accepted texture properties.
- Permit the ICD's retained CPU texture image to become a DirectDraw/local-
  VRAM texture surface in the accepted formats.
- Preserve the logical CPU image independently of residency for eviction,
  surface loss and mode changes.
- Ensure `flush`, `finish`, readback, clear and SwapBuffers use the shared
  bounded drain/coherence contract.

### Required core correction: fallback eligibility

`v9x_r3d_fallback()` currently allowlists the software renderer only for Gen3.
Do not merely add Mach64 to that conditional. First measure:

1. hardware RGB565 and XRGB1555 encodings against the CPU rasterizer;
2. hardware Z16 encoding and comparison direction against CPU Z;
3. hardware-to-CPU and CPU-to-hardware color/Z ordering;
4. texture upload visibility after CPU writes;
5. blended overlap across an engine boundary;
6. scissored whole-draw fallback with no double-rendered triangle.

Only an all-green matrix permits Mach64 whole-draw fallback. Otherwise the ICD
must describe only the hardware subset or use a separate CPU-owned frame and
explicit composite path; mixed rendering into one target would be unsafe.

### OpenGL gates

- `V9XGLP.EXE` obtains an ICD format and proves each Phase 4 state uses
  hardware counters.
- Compare its scenes with the generic OpenGL implementation and software
  Velocity9x backend.
- Run GLQuake and Quake 2 windowed and fullscreen, including context restart,
  `vid_restart`, front-buffer loading draws and both z-trick settings.
- Record hardware draws, fallback draws by reason, drain time, CPU time,
  texture uploads/evictions and screenshots.

**Done:** the probe is correct with hardware actually active; every fallback
is selected before emission; GLQuake and Quake 2 survive repeated context,
mode and surface-loss cycles without corruption or deadlock.

## Phase 7 — mode changes, presentation and native scanout decision

Hardware rendering may initially coexist with VBE mode setting, but live mode
switching must become an explicit ownership boundary:

1. drain the engine;
2. invalidate texture/surface residency and increment the render generation;
3. perform the VBE mode switch;
4. rediscover LFB, pitch, target format and available heap;
5. remap/revalidate MMIO if required;
6. reset or reinitialize the draw engine and replay baseline state;
7. restore DirectDraw/OpenGL surfaces or report loss for recreation;
8. redescribe the render interface before another draw.

Test 640x480x16 -> 800x600x16 -> 1024x768x16 -> 640x480x16, windowed/fullscreen
transitions, Alt-Tab, DOS-box entry/exit and Disable/Enable. A mode that cannot
fit the requested 3D surfaces fails creation cleanly rather than overlapping
the visible framebuffer.

The BIOS can also reprogram the chip without any mode change: the Fn+F5
LCD/CRT toggle does so unless `SCRATCH_REG3.DISPLAY_SWITCH_DISABLE` is set
(audit erratum E5). Decide whether the driver sets it before hardware 3D
ships. If it does not, add Fn+F5 during rendering to the test list above.

After copy-based presentation is stable, decide separately whether native
CRTC/vblank/page-flip ownership is worth its panel and DSP risk. It is not
silently absorbed into this engine plan. If accepted, it needs its own
read-only timing capture and physical-write gate because the Mobility LCD
shadow registers and DSP calculation are not emulated by 86Box.

**Done:** every supported live transition either restores a usable hardware
context or reports surface loss for reconstruction, with no stale address
reused and no cross-mode VRAM overlap.

## Phase 8 — release gate

Release is a separate decision from successful diagnostics.

Required evidence:

- host suite and tree checks green;
- a VT2 or any unmatched chip ID, revision or mode performs no Mach64LM
  engine write;
- two cold-boot passes of the complete 2D/3D diagnostic matrix;
- one-hour alternating D3D/OpenGL/2D stress at 640x480 and 800x600;
- repeated live mode switching and surface loss/restore;
- no FIFO timeout, reset, guard corruption or unexplained error status;
- hardware counters prove hardware execution for every advertised feature;
- unsupported features refuse or use a proven whole-draw fallback;
- clean uninstall/reinstall and rollback to the tier-0 ATI package.

Performance numbers are observations, not correctness gates. Record triangle
rate, texture upload bandwidth, FIFO reads per submitted register, idle/drain
time and fallback cost so later optimizations have a baseline.

## Representative file changes

The exact split may evolve, but responsibility should remain recognizable:

| Area | Expected files |
|---|---|
| Engine ABI | `include/velocity9x/engine_abi.h`, shared descriptor definitions |
| ATI register subset | `include/velocity9x/ati_mach64_regs.h` or equivalent project-owned header |
| 16-bit identity/early descriptor | `src/chipsets/ati/mobility/mobility_hw16.c`, `src/chipsets/ati/ati_hw16.c`, `src/display16/dd16.c` |
| Shared MMIO/FIFO/reset/coherence | `src/display32/engines/eng_mach64.c` plus a small internal header |
| DirectDraw engine selection | `src/display32/ddhal_core.c`, `src/display32/ddhal_internal.h` |
| Direct3D engine | `src/display32/d3d/d3d_mach64.c`, `d3d_internal.h`, `d3d_core.c` |
| Neutral render fallback | `src/display32/d3d/d3d_core.c`, `src/display32/r3d/*` only where the existing contract lacks a measured Mach64 case |
| Family declaration | `packaging/families/ati/family.psd1` |
| Diagnostics | `tools/diag/` and capture validators in `scripts/` |
| Host tests | FIFO/MMIO transcript, state translation, caps selection, VRAM layout, mode-generation and refusal tests under `tests/host/` |

Do not place Mach64 register or capability decisions in chip-neutral files when
an engine operation or limits field can carry them. Do not extend append-only
positional structs by inserting fields in the middle.

## Validation matrix

| Venue | What it can prove | What it cannot prove |
|---|---|---|
| Host tests | arithmetic, state translation, caps, validation, allocation, transcript ordering, refusal and recovery policy | silicon behavior |
| 86Box Mach64 VT2 | pre-VTB 2D register addressing, common 2D command construction, package and lifecycle regressions | Rage setup engine, Mobility FIFO depth/timing, cache hazards, LCD/DSP behavior |
| Gateway Solo 2150 | Mach64LM MMIO, FIFO, reset, 2D/3D results, VRAM pressure, cache coherence, panel/live switching | broad model/revision coverage |

Only the physical laptop can close a 3D phase. Evidence from one Mobility-M
revision must not silently enable other Rage PCI IDs.

## Risks and mitigations

- **A status-register mistake wedges the machine.** Exact-batch FIFO
  reservation, bounded waits, one reset, one feature per build.
- **CPU reads stale VRAM.** One shared drain/invalidate boundary used by
  readback, fallback, screenshots and texture updates.
- **The copy-commit erratum corrupts presentation.** Idle after every copy
  until a dedicated physical experiment supports relaxing it.
- **Wrong early engine selection publishes ViRGE caps.** Remove the implicit
  ViRGE publication fallback before setting ATI's D3D capability bit.
- **4 MiB allocations overlap.** One mode-aware allocator, heap exclusion,
  guard regions and refusal at 1024x768 where the surface set does not fit.
- **Mixed hardware/software frames disagree.** No Mach64 fallback allowlist
  until color, depth, ordering and blend-boundary tests all pass.
- **VBE reprograms state behind the engine.** Mode changes are drain,
  invalidate, VBE set, rediscover, reinitialize, redescribe—not continuation.
  The Fn+F5 display switch is the same hazard without a mode change; see
  Phase 7 and erratum E5.
- **Documentation covers close relatives rather than every LM quirk.** ATI
  guides establish the register model; every shipping claim is narrowed by
  physical measurement on `4C4D` revision `0x64`.

## Out of scope

- Mach64 VT2 hardware 3D—it has no Rage setup engine in the target model.
- Supporting additional Rage/LT/XL/Mobility PCI IDs without their own probe
  evidence.
- A separate ATI OpenGL ICD.
- AGP texture heaps, AGP command DMA and bus-master register lists.
- Two texture units or an OpenGL multitexture ABI revision.
- Mipmapping, trilinear filtering, VQ texture compression or 32-bpp targets.
- Native LCD timings, DSP programming and page flips before Phase 7 makes a
  separate ownership decision.
- Performance claims against ATI's stock driver before the correctness and
  stress gates pass.

## Source basis

- ATI, *RAGE PRO and Derivatives Programmer's Guide*, revision 1.0,
  March 2000: `PRG-215R3-00-10`.
- ATI, *3D RAGE LT PRO Register Reference*, revision 2.01x, June 1998:
  `RRG-G03300`.

  The only local copies of these two PDFs are
  `tmp\pdfs\ati-rage-pro-programmers-guide.pdf` and
  `tmp\pdfs\ati-rage-lt-pro-register-reference.pdf`. `tmp\` is untracked, so
  they need a durable home before Phase 1 cites them in register code.
- X.Org ATI/Mach64 driver documentation:
  <https://www.x.org/releases/X11R7.0/doc/html/ati3.html>.
- Local MIT-licensed X.Org Mach64 source at `C:\everything\xf86-video-mach64`.
- Historical Mesa Mach64 DRI source at
  `C:\everything\claude\personal\3dfx driver research\artifacts\MesaFx-7.4.4\src\mesa\drivers\dri\mach64`.
- Velocity9x hardware audit:
  [`../decisions/2026-08-16-ati-mach64-hardware-audit.md`](../decisions/2026-08-16-ati-mach64-hardware-audit.md).
- Shared OpenGL/render architecture:
  [`opengl-1.1-icd.md`](opengl-1.1-icd.md).
