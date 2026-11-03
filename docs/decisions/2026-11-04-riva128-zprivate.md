# RIVA 128ZX: the D3DZP checks pass; they only run under /zprivate

Date: 2026-11-04

Machine: M-7V636C (remote help), RIVA 128ZX 8 MiB, Windows 98 first
edition, boot 32 (b5d1e2ba), desktop 800x600x16 at 5:5:5. File:
`docs/probe/m7v636c-riva128-games-2026-11-04/b32z-V9XDDT.TXT`.

## What the list said

The open list since boot 26 named D3DZPCompareOk and D3DZPWriteMaskOk as
"Z with perspective" failures. They are neither. ZP is V9XDDP's
private-device depth test: a private render target with a depth surface
attached before CreateDevice. It runs only under `/zprivate`, and a
default run writes 0 for both keys and leaves the rest at the "not run"
seed 88760231h (D3DZPrivateRun 0). Every default run since boot 26 was
like that.

## V9XDDP /zprivate, boot 32

- D3DZPrivateRun 1, D3DZPIsHardware 1. The depth surface is in video
  memory (caps 10024000h) at pitch 128.
- Compare ladder: Init 7C00h, Reject 7C00h, Accept 001Fh, Update 001Fh.
  D3DZPCompareOk 1.
- Write mask: Mask 7C00h, NoWrite 03E0h. D3DZPWriteMaskOk 1.
- In that mode V9XDDP skips its shared-target depth, sprite, ramp and
  chain-count checks, which read 0 there by design. 201 of the 215 `Ok`
  keys it writes are non-zero.

No change to the driver. Of the default run's 14 zero keys, 12 are real.
