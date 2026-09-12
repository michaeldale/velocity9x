# Intel Phase 1 is code-ready, not hardware-complete

Date: 2026-09-12

The `intel-gma` package now contains the read-only MMIO fingerprint instrument
required by Phase 1 of `docs/plans/hardware-d3d-on-intel-gma950.md`.

The 16-bit Intel diagnostics path reads PCI BAR0 at runtime. It rejects an I/O
BAR, a base below 16 MiB, a base that cannot contain the full 512 KiB window,
or a base not aligned to that window. The Intel-only mini-VDD maps that exact
window once and refuses a changed BAR rather than accumulating permanent
mappings. It reads the fixed 20-dword allowlist twice and exposes the two
values, offset and BAR provenance through the version 3 protected-mode API.
No Intel MMIO write is present.

After the VBIOS mode has been enabled, the display driver writes
`C:\V9XDIAG\INTELMM.TXT`. The file includes every raw value, repeat-read XOR
delta, the selected live pipe, decoded timing/source/plane fields, and the
complete fingerprint flags. `Result=PASS` requires stable and nontrivial
reads, exactly one coherent live pipe, valid timing, matching source and plane
geometry. Ring quiescence is reported independently because it becomes a gate
in Phase 3, not Phase 1. A partial relationship is `Result=REVIEW`; capture,
contract and decode failures are named separately.

This decision does not declare Phase 1 complete. Completion still requires a
physical 945GSE capture whose relationships agree with the live 1024x576 mode.
Phase 2 GTT work remains gated on that evidence.
