# Review follow-up: filter submissions and clear/flip records

2026-09-20, following review of `dfd9548..3b27391`. ABI 2026092005.
These changes improve the evidence and correct the watermark cache; they
are not a measured fix for 3DMark's filter report or Final Reality's flicker.

The intel95 texture-state conclusion is withdrawn in its decision record.
A zero stored handle does not prove the application asked for no texture:
a rejected state block can leave the handle at zero. State persists, so a
small number of rejected blocks can affect many draws. The latest rejection
now records its reason, context, count, offset and retained texture handle.
Those values do not expose the contents of an invalid execute buffer.

`DrawsMagLinear` now increments alongside successful textured submissions;
`DrawsMinLinear` does the same for minification. A bind followed by a refused
stream no longer increments MAG. These are submitted sampler-state counts,
not measurements of which filter produced a pixel. Failed submission waits
are excluded, just as they are from `I9xxTextureDraws`.

`BltFlipLogCount` counts engine attempts started while a flip was pending.
The latest sixteen records occupy a ring: entry N goes in slot N modulo 16,
with N starting at zero. The next write slot is `BltFlipLogCount % 16`.
Each `BltFlipNN` entry contains:

| suffix | meaning |
|---|---|
| `Seq` | accepted flip sequence sampled before engine dispatch |
| `Op` | 1=copy, 2=colour fill, 3=depth fill |
| `Outcome` | 0=engine programmed, 1=busy, 2=declined |
| `Dest` | destination surface offset |
| `Source` | copy source offset, or `0xFFFFFFFF` for a fill |
| `Retiring` | outgoing surface offset from that flip's `lpSurfCurr` |
| `Pending` | incoming offset requested by that flip |

Identity is captured before dispatch and the outcome attached afterwards.
Only outcome 0 describes an engine submission; busy and declined attempts
are not executed clears. A declined operation may subsequently use the CPU
fallback, which this engine record does not describe. The older
`BltEngineFlipPending` and last-destination fields remain attempt statistics.
Retiring/pending are logical flip-chain addresses, not proof of scanout
ownership at the instant of the blit. An invalid surface offset is
`0xFFFFFFFF`. No flip wait was added.

The watermark cache now includes the resolved live plane. Switching planes
with otherwise identical inputs therefore recalculates which FIFO gets the
active watermark; the cache resets that identity at DriverInit as well.

Validation: `scripts/run-checks.ps1` passed, including host tests, validators,
all five family builds and package audits. Hardware validation remains open.
