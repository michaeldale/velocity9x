# Intel Phase 4: the 945GSE internal-buffer erratum keeps the write gate closed

Status: accepted, 2026-09-12. Phase 4 implementation may continue behind the
arm contract; no Intel MMIO write may become reachable yet.

## Evidence

The primary source is Intel's November 2009 *Mobile Intel 945 Express Chipset
Family Specification Update*, document 309220-0132:

<https://www.intel.com/Assets/PDF/specupdate/309220.pdf>

The measured host bridge is `8086:27AC` revision 03, which the update identifies
as Mobile 945GSE A3. Its summary table marks both errata below as applying to
GSE A3 and as having no silicon fix.

- Erratum 7 can hang or blue-screen an extended 3D workload on battery when
  Dual-Frequency Graphics Technology is enabled. Intel's published workaround
  is to disable DFGT in the graphics driver. Phase 4 is not a 3D workload, but
  physical experiments remain AC-only until that register state can be proved.
- Erratum 12 can issue an incorrect internal-buffer flush for a particular
  sequence of processor and integrated-graphics memory accesses, with a system
  hang as the implication. Intel names fixed Windows XP and Vista driver
  versions but publishes neither the triggering sequence nor the workaround.

The public Linux i915 sources and Intel programmer references inspected for the
ring and BLT command definitions do not identify a 945GSE-specific workaround
that can be tied to erratum 12. A generic cache flush or `MI_FLUSH` must not be
called the workaround without evidence: the erratum itself concerns an
incorrect internal-buffer flush.

## Decision

The erratum gate is a separate input to the one-shot arm state machine and is
false in the physical driver. A valid token, exact PCI identity, revision,
phase and command CRC cannot override it. This makes an accidental SYSTEM.INI
arm insufficient to reach a write.

Development continues on code that is safe without executing the GPU:

- the top 128 KiB is excluded from DirectDraw on the Intel family;
- ring, HWS and scratch layout is deterministic and host-tested;
- ring space and wrap arithmetic is host-tested;
- emitted commands pass an exact allowlist decoder before submission;
- the arm contract and command CRC are host-tested.

Opening the gate requires a later decision record that identifies and
implements a licence-compatible workaround, or explicitly supersedes the
plan's kill criterion with a narrower risk decision. Until then the package
must remain incapable of Intel MMIO writes.
