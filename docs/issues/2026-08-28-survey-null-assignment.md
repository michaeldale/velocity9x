# The survey pointed the video BIOS at DS:0000 and corrupted the machine

Date: 2026-08-28
Status: **cause found and fixed in source; the fixed binary is untested on the
machine that showed it**

Reported by CentaurHauls on the Acer NAV50, on a freshly wiped Windows 98 SE
install: `V9XSURV.EXE` finished writing its report, printed

```
*** NULL assignment detected
```

and froze. It had done the same on the previous Windows Me install, which is
why the message is not a one-off.

This is the worst class of defect this tool can have. It is handed to
strangers on the promise that it reads and does not alter their machine, and
the safety gate exists to make that a property rather than a claim. The gate
watched every port and every opcode and never looked at what the tool was
telling the BIOS to write to.

## The message is a diagnosis, not an error

`*** NULL assignment detected` is the Open Watcom DOS runtime's exit-time
check on the null pointer zone: it stamps the first bytes of DGROUP at startup
and compares them on the way out. It fires when something has written through
a null or near-null pointer during the run. Its presence is proof that DS:0000
was rewritten.

## What wrote there

`vbe_call` took the buffer as an optional argument:

```c
    segread(&segments);
    ...
    if (buffer != 0) {
        segments.es = FP_SEG(buffer);
        input.x.di = FP_OFF(buffer);
    }
    int86x(0x10, &input, &output, &segments);
```

`segread` returns the caller's ES, which in the small model is DS, and the
register block was zeroed just above. So on a call with no buffer, ES:DI
reached the BIOS as **DS:0000** - a pointer to the tool's own null zone.

Two call sites pass no buffer:

| Call | What it is |
|---|---|
| `4F03h` | get current VBE mode; answers in BX |
| `4F15h` BL=00h | report DDC capabilities; answers in BX |

Neither is documented to write to ES:DI. Documentation is not what runs. A
BIOS that deposits anything at ES:DI on either call writes it over DGROUP
offset zero, and this Pineview BIOS evidently does.

## The fix

A named scratch buffer, and ES:DI set unconditionally from whichever
destination applies:

```c
static unsigned char vbe_no_buffer_scratch[256];
...
    if (destination == 0) {
        destination = (void far *)vbe_no_buffer_scratch;
    }
    segments.es = FP_SEG(destination);
    input.x.di = FP_OFF(destination);
```

128 bytes is an EDID block, the largest thing these functions could plausibly
deposit; the rest is margin.

The safety gate gains its first **required** rules to go with its banned ones -
the scratch must exist, a null buffer must be substituted for it, and ES must
be set from the resolved destination. The self-test grew a `Remove` mutation
shape to exercise them, because a rule about an omission cannot be tested by
appending. Twelve mutations now, all rejected.

## What this probably also explains

Both NAV50 reports are missing the `[VBEModes]`, `[EDID]` and `[VGARegisters]`
section headers and their fixed keys, and neither carries the final
`[Result]` - filed separately as
`docs\issues\2026-08-28-survey-report-sections-missing.md`.

The ordering fits. `survey_vbe` writes the `[VBE]` keys, then makes the
bufferless `4F03h` call, and the very next thing it would write is
`[VBEModes]` - the first section that went missing. The bufferless `4F15h`
sits just before `[EDID]`, the second. A BIOS scribbling over low DGROUP
between those points is a credible way to lose stdio state or a global while
the indexed rows written afterwards still land.

**Credible is not measured.** The two issues stay separate until a report from
the fixed binary says whether the sections came back.

## What is still unknown

Whether the fix stops the freeze. The null-zone check firing proves DS:0 was
written; it does not prove that was the only damage, and the machine froze
*after* the message, which is after `fclose` and after the runtime's own
teardown began.

What the BIOS actually wrote, and on which of the two calls. The scratch
buffer is not inspected or reported. If the next run still misbehaves, dumping
`vbe_no_buffer_scratch` after each bufferless call would say which function
touched it and with what.

## Next

Send CentaurHauls a rebuilt `V9XSURV.EXE` and ask for one more `/rom` run from
real DOS. Three things to look at, in order: whether the message is gone,
whether `[Result] Complete=yes` is present, and whether the mode table still
reads the same six describable modes out of thirty-six - which it should, since
that finding is now confirmed from three independent measurements
(`docs\decisions\2026-08-28-pineview-vbe-mode-list.md`).
