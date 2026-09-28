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

After a user-confirmed physical power cycle, the unchanged Build B payload ran
again as job `ati-phase3-cold2-20260928-a`. It exited 0 in 27 ms and produced
`ATI3D0-COLD2.TXT` and `ATI3D0-COLD2.BMP`, byte-identical to both accepted
same-boot captures above. The agent reported 377,756 ms uptime immediately
before this run, consistent with the fresh power-on. Its persistent
`BootCounter` remained `9`, so the evidence records the counter discrepancy
instead of presenting it as independent reboot proof. A final health check
found the desktop ready and the agent responsive. Phase 3's two-cold-boot
scene gate is complete.
