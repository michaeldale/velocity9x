# Riva 128: V9XD32 T22's 32-bit textures are taken and draw black

Date: 2026-11-17

Machine: M-7V636C (Mac's Riva 128, `12D2:0018`), boot 68, 800x600 at
16 bpp (5:5:5). Its installed nvidia set was not identified beyond its
V9XHW.INI. Status: open, cause not established.

Evidence: [`../probe/nv3-modulate-alpha-m-7v636c-2026-11-17/d32-bpp16/V9XD32.TXT`](../probe/nv3-modulate-alpha-m-7v636c-2026-11-17/d32-bpp16/V9XD32.TXT),
run with `V9XD32 /bpp16` (the default 32 bpp ends at `NO-HAL-AT-DEPTH`:
the HAL renders at 16 bpp only).

## What was seen

T22 (T6's quadrant map as an A8R8G8B8 and an X8R8G8B8 texture) is newer
than the last recorded Riva run (`m7v636c-riva128-scissor-2026-11-07`),
so this is its first run here.
- `T22ACreateHr`, `LockHr`, `HandleHr` and `DrawHr` are all 0, and the
  same for X.
- All eight judged pixels are 0 (black): `T22AOk=0`, `T22XOk=0`.
- That adds two to `Failures` (3 now, against 1 on 2026-11-07). The
  older failure is unchanged and was not traced here.

T22 is meant to be skipped, not counted, where the driver does not take
the format. Here the surfaces were created and the draws reported
success, but nothing visible was drawn.

## Hypotheses, none tested

- The NV3 engine cannot sample a 32-bit texel, the HAL refuses those
  draws internally, and `DrawHr` still reports success, so the band
  keeps its clear colour. If so, the refusal counters in a V9XTRACE
  snapshot rise during T22.
- The Direct3D texture format list advertises a 32-bit format the
  engine cannot draw. If so, the fix is to stop listing it, which makes
  T22 skip.

## Next

V9XTRACE snapshots before and after `V9XD32 /bpp16`, comparing the
refusal counters, and the texture formats the HAL enumerates.

## 1.0 re-read, 2026-11-26

Stays open for 1.0 and is named in the README's known issues. The RIVA
128ZX is Experimental for 1.0; the README's entry links this record.
