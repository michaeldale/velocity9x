# GDI acceleration, build 005: text on the Trio64

Date: 2026-09-06
Status: **implemented; verified on the 86Box Trio64 guest after one defect
found and fixed there; default off; not yet run on a card.** The defect and
what killed it are in "What the guest run found" below. Every remaining
hardware statement is a hypothesis with the measurement that would confirm or
kill it named beside it.

Build 005 was planned as extra ROPs and re-targeted by
[the next-steps record](2026-08-27-gdi-accel-next-steps.md): CrystalMark on
BARRY scored Text at 3 against Square at 275, so text was where the 2D
headroom was and the ROPs would have moved nothing. That record also said the
highest-value work was the one that could not be validated on the hardware
here, because the Trio64 declined monochrome upload. This build closes that
gap the other way round: text is implemented **on the Trio64 first**, so BARRY
can measure it, and the ViRGE declines for now.

## What ships

| | |
|---|---|
| Key | `GdiAccelText` in `[Velocity9x]`, **default 0** |
| Advertised | `V9X_GDI_PRIM_TEXT` = `0x20`, so `Advertised=55` everywhere |
| Trio64 | implemented: ordinal 14 dispatcher, two DIB Engine callbacks, one engine primitive |
| ViRGE/DX | declines at the engine-type gate (reject bit 8) |
| ati, vbe, matrox-m2 | decline at the first gate; one flag test per call |
| `Acceleration=` | gains a `-text` suffix when on: `gdi-fill-copy-overlap-text` |

## The mechanism is the DIB Engine's, not ours

Every Windows 98 DDK display sample accelerates text the same way, and none of
them touches a font. The driver's `ExtTextOut` (ordinal 14) checks that the
call is a plain screen draw and then jumps to **`DIB_ExtTextOutExt`** with two
extra far pointers appended below the return address: a callback that draws a
monochrome bitmap, and one that fills a rectangle. The DIB Engine does the
layout - glyph lookup, spacing, clipping - renders the whole string into one
1-bpp buffer, and calls back with that buffer, its position, its clip, and the
two physical colours. If the call was opaque it calls the rectangle callback
first.

Read from `98DDK\src\display\mini\xga\STRBLT.ASM:78-135` and `:152-303`, with
the framebuf sample's `TSENGTXT.ASM:96-172` and the S3 sample's `STRBLT.ASM`
and `TEXT_BT.ASM:60-240` as the two other witnesses. What was taken is the
argument order of the three entry points, the meaning of the callback's
`Flags` bit 0 (transparent), and the fact that the S3 sample clips the bitmap's
height by the clip rectangle it is given rather than trusting the bitmap. No
code was copied; the dispatcher and both callbacks are C.

So text acceleration on this platform **is** monochrome expansion, which is why
build 004's design record could say the groundwork already existed. What 004
lacked was the plumbing - ordinal 14 was an unconditional forward - and what the
Trio64 lacked was the register sequence. Both are in this build.

## The Trio64 sequence, and where each part came from

The 8514/A command set has no text opcode and no CPU-source blit. It has a
rectangle fill whose per-pixel mix can be chosen by data the CPU writes to the
pixel transfer register, and that is the whole primitive:

| Register | Value | Meaning |
|---|---|---|
| `PIX_CNTL` (MULTIFUNC 0AH) | `A080H` | bits 7:6 = 10b: the mix is selected by CPU data |
| `FRGD_MIX` (BAE8H) | `0027H` | one bits: FRGD colour register, replace |
| `BKGD_MIX` (B6E8H) | `0007H` / `0003H` | zero bits: BKGD colour, replace / leave destination (transparent) |
| `FRGD_COLOR`, `BKGD_COLOR` | the callback's two colours | A6E8H, A2E8H |
| `MAJ_AXIS_PCNT` | `words_per_row * 16 - 1` | see below |
| `CMD` (9AE8H) | `53B3H` | rectangle fill + wait for CPU data + 16-bit transfers + byte swap + **plane mode** (bit 1: the data is one bit per pixel) |
| `PIX_TRANS` (E2E8H) | the bitmap, a word at a time | `rep outsw` |

The register numbers and the mix encodings are the 8514/A's, which the fill and
copy primitives already drive on this chip. The three new command bits and the
pixel-control encoding are taken from **the emulator's model** of this engine
(`build\upstream-86box\src\video\vid_s3.c`): the word-wide PIX_TRANS handler
routes to a 16-count monochrome mix when `CMD` bit 9 is set and `PIX_CNTL` bits
7:6 read 10b, swaps the bytes when `CMD` bit 12 is set, and the rectangle fill
consumes bit 15 of each word first. That emulator special-cases the command
value `53B3H` by name, which is a strong hint it is what period drivers wrote,
and no more than a hint. (The first build wrote `53B1H` and the guest run
showed why bit 1 matters - see "What the guest run found".)

**Hypotheses, each with its measurement:**

1. **Byte swap.** The DIB Engine's buffer has the leftmost pixel in the top bit
   of the first byte; a word read from it in the CPU's byte order puts the
   *second* eight pixels in bits 15:8. The emulator's swap bit corrects that.
   If real silicon reads the bit the other way, every glyph draws with its
   byte columns transposed - which the pixel comparison catches on the first
   string and which is unmissable on a screen. Fix if wrong: clear bit 12 and
   swap in software, or feed bytes.
2. **Row continuity.** The emulator runs rows together with no padding: a row
   of 24 pixels fed as two words spills eight bits into the next row. This
   build sidesteps the question rather than answering it, by always drawing a
   rectangle `words_per_row * 16` wide and clipping the extra columns with the
   scissors. Whichever way hardware behaves on partial words, whole words are
   unambiguous. An odd trailing byte is written as a word on its own rather
   than read from the buffer, because the byte after it is the next row or the
   end of the selector.
3. **FIFO pacing.** Nobody here has the databook page saying what a Trio64 does
   with a write to a full command FIFO. The feed therefore waits for the FIFO
   to read empty (low byte of `9AE8H` all zero, which means empty under either
   8514/A bit convention) before every burst of eight words - the original
   adapter's depth. Cost: one port read per eight words. If hardware inserts
   wait states, this is wasted and `V9X_TRIO_FIFO_BURST_WORDS` can grow; if it
   drops writes, this is what keeps text intact. 86Box models a FIFO that is
   never full, so emulation cannot decide this - **only BARRY can.**
4. **The clip the DIB Engine passes bounds the string.** The callbacks draw
   the whole `WidthBytes * 8` wide bitmap with opaque background through the
   scissors set to `lpClipRect`, intersected with the string's own rectangle
   and the surface. If the DIB Engine's clip were wider than the string's true
   pixel extent, opaque text would paint up to seven background columns past
   its last glyph. The xga and S3 samples draw exactly this way, and the pixel
   comparison against the DIB Engine's own software rendering fails on the
   first opaque string if the assumption is wrong.

## Two things this build does differently from fill and copy

**The primitive waits idle before returning.** Fill and copy return with the
command in flight and rely on the dirty flag: the next CPU access to the
framebuffer goes through `deBeginAccess`, which drains. The text callback
cannot rely on that. It runs *inside* `DIB_ExtTextOutExt`, which excluded the
software cursor before calling and un-excludes it - with CPU writes that do not
pass through `deBeginAccess` - the moment the callback returns. So the engine
must be idle by then, and the opaque-rectangle callback waits for the same
reason. A string's worth of engine time is microseconds; the wait is a port
read.

**The scissors are narrowed, then restored behind the data.** Clipping is what
the DIB Engine has delegated, and the engine's clip registers are the only way
to do it without touching the bits. Fill and copy open the scissors wide in
`v9x_gdi_trio_prepare`; the 32-bit HAL programs them not at all. So a
narrowed clip left behind would cut the next DirectDraw blit, and the text
primitive writes the four wide-open values and the plain pixel-control word
after the data, in FIFO order, where they execute once the expansion is done.

## The fallback: a callback cannot decline

By the time a callback runs, the DIB Engine has decided the driver is drawing.
So a callback that cannot proceed - the wrong destination, a bitmap that would
cross its selector, a negative origin, a bounded wait that expired - sets a
DGROUP flag and returns, and the dispatcher, which still holds all twelve
original arguments, calls `DIB_ExtTextOut` and has the string drawn again in
software. The opaque rectangle may be painted twice that way; a glyph is never
painted zero times. `text_fallbacks` counts it, and the harness asserts the
count does not move, because nothing it draws is meant to need this.

A bounded wait expiring *during* the feed does not stop the feed. The engine
has been told how many pixels to expect, and the remaining words are the
cheapest way to let it finish and settle - whether the wait really failed or
was a fault injection - before the software redraw paints over the result.

## Gates, in order

Extent call (negative count), `ETO_LEVEL_MODE`, destination not the screen
PDEVICE (pointer compared before any field is read - a memory-bitmap
destination is a plain `BITMAP`, build 004's lesson), `VRAM` clear, `BUSY` or
`PALETTE_XLAT` set, depth not 8 or 16, surface base not on a scan line (the
Trio64 folds the base into y), engine not a Trio64. Each records a bit in
`text_reject_mask`; the callbacks add bits 9-13 for their own refusals. With
text enabled the harness allows bits 0, 2 and 4 only - accepted, extent call,
and this run's own reference bitmap as destination.

`StrBlt` (ordinal 11) still forwards to the DIB Engine. It is the Windows 2.x
entry, GDI has issued text through ordinal 14 since 3.0, and `DIB_StrBlt` does
not come back through the driver.

## What the harness now does and asserts

`V9XGDI /accel` gains two operation kinds out of twelve: `TextOut` alternating
opaque and transparent background across two stock fonts, and an `ExtTextOut`
with an opaque rectangle overhanging the string under a clip region that cuts
into the glyphs from three sides. Both DCs get the same font, colours and
background mode, so the reference is the DIB Engine's software rendering of the
same string and the comparison is exactly the engine expansion against it.

Four new checks, in the shape 002-004 established:

- `text-enabled-but-never-fired` - with text on, `text_bitmaps` must move.
- `text-fell-back-to-software` - with text on, `text_fallbacks` must not.
- `text-unexpected-reject-reason` - with text on, the mask is bits 0, 2, 4 only.
- `text-dispatcher-never-called` - unconditional: ordinal 14 must be reached,
  on every family, or no text check means anything.

The binary audit asserts that `gdi_accel.obj` exports `EXTTEXTOUT`, that its
decline branch reaches `V9XDIBEXTTEXTOUTCALL` and its accept branch
`V9XDIBEXTTEXTOUTEXTCALL`, that both forward to the DIB Engine entries they
name, and that `dib_thunks.asm` no longer forwards `ExtTextOut`.

## What the guest run found

The first build wrote `53B1H`. With `GdiAccelText=1` on the 86Box Trio64 guest
at 800x600x16 the desktop came up with every icon label reduced to fragments
except the longest ones, and `V9XGDI /accel` failed its first comparison with
839 wrong bytes. Notepad showed the shape of it: on every line the left part
was garbage and the right part correct.

A diagnostic was built rather than a theory: `V9X_GDITEXTDUMP`, an escape
returning the last callback's arguments and the head of its bitmap, and a
`V9XGDI /textdump` mode that draws `ABCDEFGH` in the fixed system font, reads
the dump, and renders the same string into a 1-bpp memory bitmap beside it.
Two things came out of it, one about the DIB Engine and one about the engine.

**The DIB Engine's buffer has a leading pad byte, and x is passed already
shifted for it.** For a 64-pixel string drawn at x = 200 the callback received
`x = 192`, `WidthBytes = 12`, `Height = 15`, a clip of exactly `200..264 x
300..315`, and rows of `00 <8 glyph bytes> 00 00 00`. Byte 0 is eight blank
pixels at x = 192, the glyphs start at byte 1 and x = 200, and the row is
padded to a dword. The glyph bytes were the reference bitmap's bytes inverted
(`18` against `e7`, `7c` against `83`), so the buffer was being read correctly
and hypothesis 4 held: the clip the DIB Engine passes is the true string, and
drawing the whole padded width through it is right. This also explains the
DDK framebuf sample's `inc si` / `add edi, x + 8` without a comment.

**The engine consumed eight pixels per word, from the first byte only.** A
pixel readback of the drawn region showed 7 full rows and a partial eighth
where 15 were programmed, and the drawn row `r` was the even bytes of buffer
rows `2r` and `2r + 1` laid end to end. That is exactly half the data, and the
emulator's source said why: at 16 bpp it halves the count of a CPU-data word
unless the command carries **bit 1**, which is the 8514/A plane-mode bit -
"the pixel transfer data is one bit per pixel". Without it the data is colour
pixels, one per word at this depth, and only the top byte of each is ever
consulted as a mix bit. `53B1H` was the solid fill's bits plus CPU data, width
and swap; it was missing the one bit that says what the data *is*. `53B3H` is
the word the emulator special-cases by name, and it is what period drivers
wrote. With it the desktop, Notepad and the harness all came right.

The remaining observation: the reject mask carried bit 12 before any harness
run started, from 7 fallbacks during the boot, and 12 by the time Notepad and
another application had been opened. Those are strings with a negative origin,
redrawn in software by the dispatcher exactly as designed, and the desktop
shows no trace of them. Which strings they are is a follow-up; the count is
small and the fallback is the point of having one.

The harness's reject-mask check was also wrong in a way this exposed: it read
the whole session-cumulative mask and failed a run every one of whose strings
had been expanded. It now considers bits newly set during the run, and the
fallback delta check is what watches the run's own callbacks.

## Verified so far

| Gate | Result |
|---|---|
| `check-tree.ps1` | pass |
| `build-host.ps1` | pass (no new host test: nothing here is pure logic) |
| s3 family build and `audit-family-binary.ps1` | pass, with the new ordinal 14 assertions |
| `build-gdi-smoke.ps1` | pass |
| `run-checks.ps1`, all families | pass: ati, matrox-m2, s3, vbe built and audited from the same tree |
| 86Box Trio64 guest (`:9871`), 800x600x16, `GdiAccelText=1`, `V9XGDI /accel` | **PASS** with `53B3H`: 500 operations, 20 comparisons clean, `TextBitmapsDelta=84` of 84 text operations, `TextOrectsDelta=50`, `TextFallbacksDelta=0`, clipped pass 48 accelerated, injection poisoned and recovered. `Compared=FAIL` at operation 25 with `53B1H`. |
| 86Box Trio64 guest, full mode matrix | **11/11 PASS** - see below |
| BARRY, physical Trio64 | **hung within a minute of the desktop with text on**, cause fits the DOS-box ADVFUNC write ([issue](../issues/2026-09-06-text-acceleration-hangs-physical-trio64.md)); the single-string probe built for it is unrun |
| A8U4I5, the same card | **cannot host the measurement**: the machine hard-locks on framebuffer read-after-write under any driver ([issue](../issues/2026-09-06-a8u4i5-trio64-hard-locks-on-framebuffer-readback.md)) |

## The mode matrix

`run-vm-mode-matrix.ps1 -Family s3 -ChipId trio64`, reboot path, one pass,
with `GdiAccelText=1` left in the guest's `SYSTEM.INI` so every mode exercised
the text path. Results in
`build\driver-results\mode-matrix-s3-trio64-text005`.

| Mode | `/accel` | `TextBitmapsDelta` | `TextFallbacksDelta` |
|---|---|---|---|
| 640x480x8, 800x600x8, 1024x768x8, 1280x1024x8 | PASS | 84 each | 0 |
| 640x480x16, 800x600x16, 1024x768x16, 1280x1024x16 | PASS | 84 each | 0 |
| 640x480x32, 800x600x32, 1024x768x32 | PASS | 0, `depth-not-accelerated` | 0 |

Eighty-four is the number of text operations the seeded generator issues in
500, so every string in every 8- and 16-bpp mode was expanded by the engine
and matched the DIB Engine's own rendering. At 32 bpp the dispatcher declines
at the depth gate, which the harness knows and reports rather than fails.

Read that 11/11 with the same boundary the 004 record drew: every check reads
back through GDI, the emulator does not model a full FIFO, and the one
behaviour of this build that only silicon can judge is the pacing.

## CrystalMark on the guest

Two runs per column at 800x600x16, text on against text off
([record](2026-09-06-crystalmark-86box-trio64-text.md)): Text 4 and 4
against 2 and 2, with Square, Circle and Image within six points across all
four runs. Directional only - the emulator prices port I/O in its own time -
and BARRY, unreachable today, is where the number that matters gets taken.

The guest confirmed hypotheses 1, 2 and 4 and the whole dispatcher path, and
found the plane-mode bit the design had missed. It cannot confirm hypothesis
3, and it cannot rule out a behaviour the emulator does not model - which is
exactly how the ADVFUNC defect reached hardware with 11/11 emulated modes
passing. BARRY is the second run, and the decision to turn the default on
waits for it.

## Not in this build

- The ViRGE. Its `MONOSRCBLT` path exists from build 004 and would take the
  same callbacks; what it needs is the primitive and a guest run. Deferred so
  the first text build lands on the chip that can be measured here.
- A size threshold. A one-character string costs about fifteen port writes and
  two idle waits against a few microseconds of software expansion, and the
  crossover should be measured before it is guessed at. Compile-time only for
  now, as the fill threshold was before it was measured.
- `Output` (lines and curves), the hardware cursor, pattern fills. Listed in
  the next-steps record; untouched here.
