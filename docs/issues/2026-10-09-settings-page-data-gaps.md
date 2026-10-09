# What the settings page cannot show yet, by family

Date: 2026-10-09. Status: open. Raised with the two-tab redesign of the
Velocity9x Display Properties page (`tools/diag/settings_propsheet.*`).

The redesign fixed two lines that were wrong on every non-S3 card:

- **Video memory** read "Unavailable" unless the chip published a register
  decode (`VideoMemoryBytes=`, S3 only). It now falls back to `VbeVramBytes=`,
  the VBE BIOS's 4F00h total, which ati, intel-gma, matrox, sis and vbe all
  publish.
- **DirectDraw** promised "page flip" for every family that named an engine.
  It now reads the engine's capability bits: `EngineStamp=` when DirectDraw
  has stamped them since the last Enable, else the new `EngineCaps=` that
  `ddi.c` writes at every Enable from the chip's descriptor. Matrox reads
  "Engine fill and copy; no page flip" (A8U4I5 and the 86Box guest).

`ModeSwitching=vbe-lfb`, nine chips, read as "At boot"; it is live at any
depth through the shared path, measured on the 2064W, and now says so.

## Gaps

| Row | Families affected | Why | What would fix it |
|---|---|---|---|
| Video memory | s3, when CR36 decodes to an encoding the driver does not know | `VideoMemoryStatus=unavailable` and s3 publishes no `VbeVramBytes=` | s3 publishing the 4F00h total too |
| DirectDraw caps | intel-gma, between an Enable and the first DirectDraw program | `EngineCaps=` is the descriptor's answer at Enable; Gen3 maps its engine later, so the bits can be lower than what runs | Reading the shared block, or re-publishing after the map |
| DirectDraw caps | any, when `EngineStamp=` is stale | The stamp survives until the next Enable clears the section | Already handled by the `EngineCaps=` fallback |
| Revision | s3 Trio64 on VLB (`*PNP0913`) | Read from `HKLM\Enum\PCI\...&REV_xx`; a root-enumerated card has none | The driver publishing its PCI revision |
| Refresh rate | every VBE-mode family | GDI reports the hardware default; the BIOS's timing is not published | Publishing the measured refresh (`V9XTIME.EXE` measures it) |
| DAC | every family | Not published | A per-chip manifest field |
| Bus type (PCI/AGP) | every family | Not published | The driver reading the device's parent bridge |
| Direct3D feature rows (texture mapping, perspective, Z buffer, maximum texture size) | every family | The HAL publishes them to DirectDraw, not to V9XHW.INI; the software rasterizer has no perspective correction | The HAL writing its device description to an INI |

## Controls shown disabled

Texture filtering, write combining (MTRR stage A is inspect-only) and Run
diagnostics are on the Advanced tab, disabled with "Not available", until
the driver has the feature.

## Change display mode button: removed

It was meant to select the native Settings tab. On A8U4I5 the lookup found
"Settings" at tab index 7 (traced to an INI), and then neither
`PSM_SETCURSEL` sent, `PSM_SETCURSEL` posted, nor `TCM_SETCURFOCUS` on the tab
control changed the page. Not understood. The button was removed; Settings
is one click away.

The tab control reported nine items with eight visible, the last also titled
"Settings". Also not understood.
