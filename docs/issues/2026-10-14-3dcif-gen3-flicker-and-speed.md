# 3DCIF on Gen3: some flicker, and some demos run too fast

Date: 2026-10-14

Status: Open, parked by Michael on 2026-10-14.

Michael watched the SDK demos on the netbook (GMA 950, boot 180, 5:6:5)
through ATI3DCIF.DLL: "it is working, there some flicker and some things
run too fast". Which demos, and what flickers, was not recorded.

The logs give a lead on speed. Cubes drew 23,475 frames and ATi-3D 11,413
in about 40 s each, hundreds a second, against 1,319 and 1,116 on the
Rage XL in the same time
(`docs/probe/netbook-gma950-3dcif-2026-10-14/demo-summaries-before-shrink.txt`,
`docs/probe/a8u4i5-rage-xl-3dcif-2026-10-14/demo-summaries.txt`). The
demos that run too fast plausibly animate per frame rather than by time,
and the flips are not paced by the retrace on this machine. Neither is
measured.

For flicker, Glide met a similar symptom on this machine: the buffer under
construction was on screen until the flip latched, and a third buffer
cured it
(`docs/issues/2026-09-18-final-reality-flicker-is-the-buffer-under-construction.md`).
Here, though, the application owns the flip chain, not the DLL.

## Next step

Name the demos and what each does wrong, by eye. Then read the flip path
on Gen3 for these chains: whether `Flip` waits for the retrace, and how
many buffers each demo creates.
