# The ViRGE idle bit lies, and it is still not the flicker

2026-09-19 night, A8U4I5 with the Trio3D/2X, a build that requires the
engine IDLE bit to hold across 32 consecutive reads before believing it on
a part with no 3D-done bit, and counts the times it does not. Final Reality
Robots, hardware Direct3D, panel recorded to
`Y:\MWD\videos\obs\2026-09-19 22-06-06.mkv`. Attached:
`2026-09-19-trio3d-idle-confirm-V9XSNAP.txt`.

## The idle bit is not a completion signal on this part

```
VirgeIdleFalseSettle=42
D3dDoneSeen=0   D3dDoneMissing=64   D3dDoneSkipped=267416
```

Forty-two times in one Robots pass, the IDLE bit read SET and then went
clear again inside a thirty-two read window - the engine was working after
all. That is direct evidence, needing no assumption about the silicon, that
**idle alone does not mean the triangles have landed on the Trio3D.**

It is the thing SUBSYS_STAT bit 1 exists to say and that this card has
never once reported. `v9x_virge_settled` has been taking idle at its word
since the give-up fired, a quarter of a million times a run, and at least
forty-two of those were wrong.

Two things this does NOT establish. The rate is 42 in 267,416 settles, and
a thirty-two read window only catches a lie that resolves inside it - a
longer one would catch more, so 42 is a floor and not a measurement of how
often idle is wrong. And nothing here says a false settle reaches the
panel.

## It does not change the picture

Single-frame luminance dips below three quarters of the local median, over
twelve seconds, against the session's other builds:

| build | dips / 12 s | median | depth |
|---|---|---|---|
| unfixed | 23 | 102.6 | 63.3 |
| edge write | 70 | 59.0 | 22.4 |
| unblank fix | 22 | 87.9 | 47.6 |
| **idle confirm (32 reads)** | **28** | 92.8 | 53.2 |

22, 23, 28 across three builds is the run-to-run spread, not an effect.
Catching forty-two false settles did not reduce the flicker.

The arithmetic says why that is not yet conclusive: a sixty-second pass
carries roughly 110 dips, and 42 false settles were caught. Even if every
one of them had caused a dip, removing them would move the count by about a
third - inside the spread these four runs show. The experiment as built
cannot resolve an effect that size.

## What to do with that

The window is the lever, and it is in the wrong place. Confirming on every
settle costs 267,416 windows a run, which is what forced it down to
thirty-two reads. But the completion that decides what reaches the panel is
the one Flip makes, and there are 262 of those. A window of several
thousand reads at Flip alone costs about a million reads in total - nothing
- and would catch a lie that takes far longer to resolve.

That is the next build: confirm hard at Flip, count separately there, and
count the dips again. If the dips fall, idle was the fault; if a long
window at Flip still catches nothing more and the dips hold, completion is
honest at the only moment that matters and the flicker is not this either.

## Engine counters, for the record

```
EngineFifoTimeouts=0   EngineIdleTimeouts=0   EngineResets=0
```

Zero again this run. Across five passes today these have read 1,1 / 1,1 /
0,0 / 1,1 / 0,0, which settles the earlier question: they vary run to run
and nothing this session changed has moved them. The withdrawal in
`2026-09-19-nothing-the-application-does-races-the-flip.md` stands.
