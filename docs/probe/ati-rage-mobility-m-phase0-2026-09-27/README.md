# ATI Rage Mobility-M Phase 0 fingerprint

This directory preserves the first register-valid Phase 0 capture from the
Gateway Solo 2150 at `10.0.1.22:9869`.

- Machine: Gateway Solo 2150, Windows 98 SE
- Adapter: PCI `1002:4C4D`, subsystem `107B:2150`, revision `64`
- Desktop: 1024x768x16
- Probe build: `ati-p0-20260927-e`
- Guest artefact: `C:\V9XDIAG\ATIMM.TXT`
- Captured report CRC32: `448C104B`

The probe was executed twice after boot counter 8 and produced byte-identical
reports. MMIO at BAR2 `F4100000` validated against `CONFIG_CHIP_ID`; the panel,
VRAM-size, pitch, LCD-selector restoration, and stability checks passed.

The initial verdict was deliberately `REVIEW`. `CONFIG_STAT0` is `00C00096`, making
`CFG_MEM_TYPE_T` equal to 6 (32-bit SGRAM at 2:1), while the plan expected code
4 from the video BIOS's generic SDRAM wording. This is a declared Phase 0 kill
condition in the original plan. The operator subsequently directed that the
repeatable physical value be accepted for this exact subsystem/revision and
that hardware work continue. Later probe builds therefore expect code 6;
block write remains disabled pending a dedicated test.

Probe build `ati-p0-20260927-f` subsequently accepted code 6 for this board.
After waking the power-managed desktop it returned `PASS`; the downloaded
report had CRC32 `DC624323`. An immediately preceding asleep-state run safely
returned `REVIEW` with command `0080` and zero BARs, demonstrating that the
identity gate still prevents MMIO access when the function is disabled.

An authorized tier-0 binding attempt preceded the successful read. Windows
booted normally but retained the stock `ATI2DRAB.DRV`/`ATI2VXAB.VXD` binding;
the attempt did not overwrite those stock binaries. It did leave PCI memory
decoding and both BARs live, which allowed the read-only fingerprint to
complete.
