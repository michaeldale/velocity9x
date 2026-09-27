# ATI Rage Mobility-M Phase 1 scratch transaction

The Gateway Solo 2150 ran `ati-p1-20260927-a` twice from
`C:\V9XREMOTE\JOBS\ati-phase1-a`. Both reports were byte-identical with CRC32
`6CF18CD0`.

The VxD refused the write unless all of these were true:

- PCI identity `1002:4C4D`, revision `64`;
- PCI memory and I/O decode enabled;
- dedicated BAR2 exactly `F4100000`;
- `CONFIG_CHIP_ID = 64004C4D`;
- `CFG_MEM_TYPE_T = 6`;
- `GUI_ACTIVE` clear before the transaction.

It then disabled interrupts for one bounded sequence, saved `SCRATCH_REG0`,
wrote `55555555`, read the pattern back, restored the saved dword, and read the
register again. The original and restored value were both `04100400`.

No draw trigger or framebuffer address was written.
