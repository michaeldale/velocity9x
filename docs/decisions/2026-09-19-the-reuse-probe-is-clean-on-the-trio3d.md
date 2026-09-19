# The reuse probe is clean on the Trio3D, at every delay

2026-09-19 night, A8U4I5 with the Trio3D/2X, build `595afe4-dirty`,
`V9XDDP.EXE /reuse`, panel recorded at 1080p60 to
`Y:\MWD\videos\obs\2026-09-19 21-52-27.mkv` (158 s, 9,504 frames).
Attached: `2026-09-19-trio3d-reuse-probe-V9XDD2.txt`.

## What the probe does and what it said

For each delay in turn it flips, waits until GetFlipStatus reports the flip
done, waits the delay, then paints the RETIRED buffer - the one just
flipped away from, which the driver says is off screen - solid green, holds
it, repaints its colour and flips back. Green on the panel is the display
still fetching a buffer the driver has released.

Four stages, thirty flips each, every one conclusive:

```
S0  delay   0 ms   FlipsOk=30  DoneOk=30  GreenOk=30  errors 0  Conclusive=1
S1  delay  17 ms   FlipsOk=30  DoneOk=30  GreenOk=30  errors 0  Conclusive=1
S2  delay  34 ms   FlipsOk=30  DoneOk=30  GreenOk=30  errors 0  Conclusive=1
S3  delay 100 ms   ...                                          Conclusive=1
```

## What the panel said

Every frame of the recording was scanned for a green-dominant picture.
There are nineteen, in one contiguous run at t=10.23 to 10.53 s - a single
0.32 s fill during the probe's setup - and **none anywhere else in 158
seconds**, across all four stages and all 120 green paints.

So the display never fetches the retired buffer, not even when it is
painted immediately after the driver reports the flip done. On this card,
**reported flip completion is not premature**, and the latch is not late
relative to it.

That is the hypothesis this session spent the evening on, measured and
dead. It also explains why the unblank fix changed nothing: it moved
completion later, and completion was not early.

## What it leaves standing, which is the one I withdrew

Every part of the presentation path is now measured honest on this card:
the flip completes when it says it does, nothing draws into the presented
buffer, and no draw, Blt or Lock arrives while a flip is pending. Yet the
panel shows a buffer holding only the clear and the sky once every eight or
nine display frames.

If the presentation is honest, then the buffer being presented genuinely
contained only the clear and the sky at the moment it was presented - which
means the application called Flip while the GPU had not finished the frame,
and the driver let it.

That is the completion question, and it is the explanation withdrawn in
`2026-09-19-the-trio3d-flicker-is-a-completion-signal-that-does-not-exist.md`.
The withdrawal was right on its own terms: the chain was asserted, not
established, and `v9x_virge_settled` does return 0 while the IDLE bit is
clear. What was never established is the other half - whether the IDLE bit
on THIS part is trustworthy while triangles are outstanding. The bit that
would say so, SUBSYS_STAT bit 1, has never once been seen on this card:
`D3dDoneSeen=0`, `D3dDoneMissing=64`, `D3dDoneSkipped=273597`. The gap the
bit exists to close was characterised on 86Box, so nothing is known about
whether the Trio3D has an equivalent.

It is the last mechanism standing, and it is untested rather than
disproved.

## The next measurement

It needs no hardware claim: after the IDLE bit says settled and the done
bit has been given up on, spin a bounded extra interval before letting the
flip proceed, and count the dips on the panel. If a delay after IDLE
reduces them, IDLE is early on this part and a real completion signal is
required - the intel86 shape, a GPU write the CPU can read back, waited on
before the flip. If the dips are unchanged, the IDLE bit is honest and the
fault is not completion either.

That is a diagnostic build rather than a fix, and it discriminates without
asserting anything about the silicon.
