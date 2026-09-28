# ATI Rage Mobility-M Phase 3 first triangle

Target: Gateway Solo 2150, ATI Mobility-M `1002:4C4D` revision `64`, subsystem
`107B:2150`, 4 MiB SGRAM, 1024x768x16 desktop, agent boot counter 9.

`ati-phase3-20260928-b` emitted one opaque flat magenta triangle into a backed
up 64x28 RGB565 surface at VRAM offset `0x00200100`. The target has 256-byte
physical guards before and after it. The stream contains 17 complete-state
writes followed by 19 setup writes; `ONE_OVER_AREA` at the documented A7
alias is the only draw trigger.

Both same-boot runs passed and produced byte-identical artifacts:

- `ATI3D0.TXT` and `ATI3D0-PASS-B1.TXT`: CRC32 `E28B783F`;
- `ATI3D0.BMP` and `ATI3D0-PASS-B1.BMP`: CRC32 `BD518408`;
- 256 changed pixels, bounding box `(8,6)` through `(38,21)`;
- zero interior, exterior, physical-guard, persistent-state, and VRAM-restore
  mismatches;
- zero timeouts and zero recovery resets;
- `GUI_STAT` idle before and after (`0x00800000`).

The initial `ati-phase3-20260928-a` run rendered the same correct image with
all mismatch counters at zero, but returned `REVIEW` because the successful
diagnostic path fell through into its timeout-recovery label and performed an
unnecessary reset. `ATI3D0-REVIEW-A.*` preserves that harness failure. Build B
adds an explicit branch to normal restoration; it does not change the state or
setup write set.

This is one cold-boot sample. Phase 3's plan gate remains open until the same
reviewed scene passes after a second real power-off/power-on cycle.
