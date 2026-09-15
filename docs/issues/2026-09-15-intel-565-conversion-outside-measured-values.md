# The 565 conversion is measured at six channel values, not established as a rule

**Status:** open limitation, carried deliberately.
**Blocks:** any claim about colour at untested values; see below.
**Chip:** 945GSE A3, `8086:27AE` rev 03, MICHAEL-NETBOOK.

## What is established

Six channel values across two triangle colours agree with
`round(v * max / 255)`. `trunc`, `round8`, `floor` and `ceil` are each excluded
**as uniform rules**. Full record:
`docs/decisions/2026-09-15-intel-565-conversion-rounds.md`.

## What is not

**Green never separates rounding from truncation.** Both tested green values
give the same field under either rule — 100 gives 25, 135 gives 33. A backend
that rounds red and blue but truncates green fits every observation exactly as
well as a uniform rounding one does.

**Everything outside the six values is inference.** Two colours support the
expectations now compiled into `v9x_i9xx_rgb565_round()` for those colours.
They do not establish the conversion for the other 250 byte values per channel,
and the function is written as though they do.

That is an acceptable trade for what it is used for — reproducing what the
hardware stores for the colours actually in use, which is what the capture
validator needs — and it is not acceptable as a general statement about the
chip. The distinction is recorded here because the function's name does not
carry it.

## Why it is not simply closed with more probes

The fill is flat: one colour per draw, so one colour's worth of evidence per
boot. Sweeping byte values means either many boots or a scene with many
colours, and a gradient-shaded triangle introduces the interpolator as a second
unknown — a disagreement would not say whether the conversion or the
interpolation produced it.

## Proposed experiments, cheapest first

1. **One green value, one boot, bundled.** A colour whose green byte separates
   `round` from `trunc` — 3 gives `round(3*63/255) = 1` against `3>>2 = 0`.
   Choose red and blue to also re-separate at fresh values, so the boot tests
   three channels rather than one. This closes the hybrid-backend question and
   costs nothing extra if carried on a boot already scheduled for another
   reason.
2. **Several flat triangles in one scene**, each a different colour, probed
   separately. The state block is re-emitted per draw already, so this is more
   vertices rather than new state, and it turns one boot into several colours'
   worth of evidence. This is the route to covering a range of byte values
   without a boot each.
3. **A gradient**, only after (2) — and only with the interpolator's
   contribution separated first, or it measures two things at once.

## What it blocks

- Any statement in documentation or a commit message that the chip "rounds",
  unqualified.
- Promoting a full-target hash or row CRC against a software reference: every
  interior pixel differs by the known conversion gap, so the reference must
  carry the Intel conversion for every colour present, and outside the measured
  values that reference is a prediction.
- Texture sampling and 8888 formats, which are not covered at all — this is
  RGB565 from an 8-bit inline vertex colour and says nothing about either.

It does **not** block Phase 6's early steps. A texture upload, a texture-stage
operation and a depth test can each be judged by whether the expected pixels
appear, using the colours already measured.
