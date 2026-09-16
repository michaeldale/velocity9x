# The texture scene: what has to be audited before any of it is written

Phase 6's first three scenes needed no capability the chip had not already
shown — they were the same packets with different vertices and colours. The
texture scene is the first that does, and almost none of it is covered by the
2026-09-14 packet audit.

That audit was scoped to "one flat-shaded, untextured, un-Z'd triangle". It
mentions `MAP_STATE` exactly once, and only to say that a Gen2 packet is
inapplicable because Gen3 loads maps *through* `_3DSTATE_LOAD_INDIRECT` and
`MAP_STATE` instead. It never derives either. The project's rule is that a
value may enter a builder only when two independently written code paths use
it, or the judgement is recorded as a judgement — so the code for this scene
cannot be written yet.

**This plan is the statement of what must be audited. It proposes no packets.**

## What the scene has to establish

Two things, and they are separable:

1. **The sampler reads the texture at all**, rather than the fragment program
   returning a constant. A single-colour texture would show this.
2. **Addressing works** — that `(u,v)` reaches the texel it names. A
   single-colour texture cannot show this, and a scene that only showed (1)
   would be read as success while the sampler returned texel zero for every
   coordinate.

Only (2) makes the scene worth a boot, and it is what fixes the texture's
shape: **more than one texel, in distinguishable colours, probed per region.**

## The unaudited surface

Everything below is currently underived. The audit has to cover each, or record
why it cannot and what the judgement is.

| What | Why it is not covered |
|---|---|
| `_3DSTATE_MAP_STATE` | Named once as the Gen3 mechanism, never encoded |
| `_3DSTATE_SAMPLER_STATE` | Not mentioned |
| `_3DSTATE_LOAD_INDIRECT` with a real payload | Audited only in its DISABLED form — an empty enable mask plus one zero dword, which is what makes the inline shader legal |
| S2 texture coordinate format | Audited only as "every unit NOT_PRESENT", the all-ones encoding |
| S4 vertex format with UV | Audited as XYZW plus colour; the audit records that the two source trees genuinely disagree about this field and that the two-use-site rule **could not be met** even for the untextured case |
| The fragment program's `texld` | The current program is two instructions with no sampler reference |

S4 is the one to be most careful about. The existing comment in the state
builder says a disagreement between S4 and the vertex dwords is "the single
most likely silent hang in the whole phase", and this change alters both at
once.

## Design decisions, with the reasoning

### The texture is painted by the GPU, not uploaded by the CPU

`XY_COLOR_BLT` is the one operation this hardware is measured to perform
correctly, and painting a small checkerboard is a handful of them. The
alternative is the CPU writing texels through GMADR, which is the access the
errata gate opened on condition of avoiding — the Phase 5 decision moved the
render-target fill to the GPU for exactly this reason.

It also keeps the aperture-read budget flat: a GPU paint costs no driver reads.

### RGB565, matching the render target

A texture format differing from the target would put a format conversion
between the sampler and the probe, and a wrong pixel could then be the
sampler, the conversion or the addressing. One unknown at a time.

### Four texels, four colours, from the measured set

Two by two is the smallest texture that tests addressing in both axes. The
colours should come from the values already measured on this part — `1C3E`,
`F325`, `3038` and one more to be chosen and predicted — so that a wrong probe
means wrong addressing rather than an unmeasured conversion.

A blit cannot paint a single texel usefully, so the texture is a 2x2 grid of
blocks rather than 2x2 texels: each block one colour, the whole thing small
enough to sit in the reserve's remaining `0x57000` bytes with room to spare.

### It lives in the reserve, above the upper guard

The target ends at `guard_upper_offset`; everything above that page is unused.
The texture takes a page there, with its own guard either side on the pattern
the render target already uses.

## The sequence, and why it is in this order

1. **Audit** the packets above, as `docs/decisions/`. No code until this exists.
2. **Leaf units and host tests** — map and sampler encoding, texture pitch and
   address maths, the extended vertex format, the sampling fragment program.
   All pure arithmetic, all testable off the machine.
3. **A decoder arm** that refuses anything the audit did not license, on the
   pattern the Phase 5 decoder already follows.
4. **The scene**, added to the table as a sixth — which needs a **further
   errata amendment**, because five is the authorised bound and it is a bound,
   not a target. Alternatively it replaces one of the answered scenes, which
   the next section prefers.
5. **One boot.**

## The scene table after this

The colour and edge questions are now answered, so three of the five scenes
have done their work:

| Scene | Status |
|---|---|
| 0 regression | Keep. It is the only thing that says a change altered nothing. |
| 1 colour | Answered 2026-09-16. Retire, or re-point at a fresh colour if another conversion question arises. |
| 2-4 edge | Answered for one slope. Retire, or keep one as a standing coverage check. |

Retiring two leaves room for the texture scene inside the existing five-draw
authorisation. That is the shape to prefer: it needs no new risk decision, and
a boot that carries two answered scenes is spending reads on questions already
closed.

## What was built, 2026-09-16

Steps 1 through 4 are done and the local gate is green. What differs from the
plan above, and why:

- **The texture is 32x32, not 2x2.** Four quadrant blits of 16x16, because a
  blit fills in dwords and two 16-bit texels share one - a 2x2 texture cannot
  be painted a texel at a time. The grid is still two by two, which is what
  tests addressing in both axes.
- **The fourth quadrant colour is `07E0`, not the fill.** It was the fill for
  one commit, and that made a probe reading `0842` mean either "sampled
  quadrant 3" or "nothing drew here" - the scene would have reported a pass for
  a draw that never happened. `07E0` is a bit pattern the blit writes verbatim,
  so unlike the other three it carries no claim of having been read back from
  this part; nothing in the scene depends on one.
- **The scene table is two scenes, not six.** Ids 2, 3 and 4 retired and are
  not reused; the texture takes id 5. This is the shape the section above
  preferred, and it needs no further errata amendment: two draws against five
  authorised.
- **The decoder takes a texture RANGE rather than a flag.** `texture_bytes`
  non-zero means textured, which is also what lets MAP_STATE's address be
  checked against the reserve instead of trusted. A flag beside the range would
  have been a second way to say the same thing, and the two could disagree.
- **The texture guard page is now read.** It had been computed by the layout
  since the texture was placed and read by nobody - a page reserved to prove
  something and never asked. Each textured scene reads it after running and the
  capture compares it against a pre-run reading, which is the independent answer
  to "did a quadrant blit run past the end of the texture" that the decoder's
  bounds check cannot give.
- **Every scene now decodes before it is staged.** Phase 6 scenes had only ever
  been CRC-checked, which says the stream is the one the generator saw and
  nothing about whether it is safe to run. That stopped being academic with
  MAP_STATE, which makes the GPU read an address of the driver's choosing.

- **The map's footprint is checked, not only its base.** Added after review
  found two streams the decoder passed clean: MS4 carrying an 8192-byte pitch,
  which leaves the address correct while the sampler reads 126 KiB past a
  2048-byte allocation, and SS3 pointing sampler 0 at map 1, which this stream
  never declares and which would read whatever an earlier client left there.
  The decoder now requires MS3, MS4, SS2, SS3 and SS4 to be exactly the values
  the audit licensed, under a new reason `P5_TEXTURE_STATE` distinct from
  `TEXTURE_FORBIDDEN` - one sends a reader to the mode, the other to the dword.
  The stream CRC was never a substitute for this: it says the bytes are the
  ones the generator produced, not that they are safe.

### What the capture will and will not fail on

Split deliberately, because two questions are being asked at one pixel:

- **Did a texel arrive?** Settled, and a failure. The four quadrant colours are
  dwords the blits write verbatim and none is the fill, so a quadrant probe
  reading something else means the texture never reached the target.
- **Which quadrant did this coordinate land in?** Open, and reported. Reading
  quadrant 2 where the build predicted 1 is the experiment's result. Failing on
  it would let the boot confirm and never inform.

Step 5, the boot, has not happened. Nothing here is hardware evidence.

## Kill criteria

- **The audit cannot double-source the map or sampler encoding** and the
  judgement is not defensible. Guessing a texture address encoding writes a
  GPU-read pointer nobody derived, and the failure mode is a fetch from
  wherever the guess lands.
- **A general shader compiler becomes necessary.** Unchanged from the parent
  plan: the fragment program is a constant because Phase 6 forbids one, and a
  `texld` that needs register allocation is the line.
- Unchanged: undocumented packet experimentation to avoid a hang.

## What this does not need

Tiling. The audit's linear-only constraint holds; a tiled texture is a
separate phase with its own arm token, and nothing here requires one.
