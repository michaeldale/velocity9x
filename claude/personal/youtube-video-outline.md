# Velocity9x — video outline

Working notes for a YouTube video about building the driver. Your list, with
additions and a suggested order. Figures below come from the repo as of
2026-09-06 (392 commits over 29 days, 8 Aug – 6 Sep 2026; v0.7.0).

## Your points, kept

- The S3 ViRGE is the best video card that exists (cold open / hook)
- The goal: an open-source Win9x driver that can beat the native driver
- Why a v9x Remote Agent was required
- 86Box / VMs
- PCI -> PCIe with the Linux box
- S3 Trio vs S3 ViRGE bugs
- S3D
- S3's documentation is good
- The Linux probe
- Docs: decisions / issues / plans
- Claude
- 2D acceleration vs Direct3D
- Virtual hardware vs real

## Major points I think are missing

1. **What a Win9x display driver actually is.** Three modules: a 16-bit
   DISPLAY.DRV talking to the DIB Engine, a 32-bit flat DirectDraw/D3D HAL,
   and a mini-VDD. Two memory models sharing one set of headers. Without this
   the rest of the video has no frame. One diagram, 60 seconds.

2. **The toolchain.** Open Watcom against the Windows 98 DDK, in 2026, driven
   by PowerShell. Nobody expects that stack to work. Also: why C89, why no
   `enum` anywhere in the tree (size is implementation-defined across the two
   memory models).

3. **"Measured, not asserted" as the working rule.** The project's expensive
   mistakes were all confident statements about silicon nobody measured. That
   became a written rule in CLAUDE.md and a doc format (decision docs carry
   the log/INI/capture and the hypotheses the evidence killed). This is the
   thesis of the video more than any single bug is.

4. **The Trio3D blend saga (4–5 Sep).** The best single story in the repo:
   two decision docs concluded the Trio3D/2X cannot alpha-blend; a controlled
   A/B the same day retracted both; then "a power cycle clears it" was
   refuted the next day. The card has two blend states and nobody knows what
   moves it between them. A public retraction chain is rare and it is what
   makes the "measure it" rule land.

5. **The benchmark against the retail driver.** Ironfield RTS BltFast: retail
   S3 driver 19 FPS, Velocity9x 18. CrystalMark 2D ahead of its own software
   baseline. Your goal is "better than native"; the honest scoreboard is 2D
   close, 3D far behind, and that honesty is the point.

6. **Live colour-depth switching on the ViRGE with no reboot.** Retail 9x
   drivers of the era did not do this. It is the one place the driver is
   ahead of native, and it demos well on camera.

7. **The generic VBE family and the Intel GMA 950 netbook.** The driver ran
   on silicon it had never been told about. Ties into "virtual vs real" and
   "S3 Trio vs ViRGE": three physical machines, three buses (PCI, VLB,
   whatever the netbook is), two vendors.

8. **The 486 VLB machine and Windows 95.** Trio64 on VESA Local Bus, the
   aperture survey (seven INI captures to answer one question), EMM386 and
   F5. VLB is a different world from PCI and most viewers will never have
   seen it debugged.

9. **The things that never worked, and the retreat list.** Matrox Millennium
   II (physical baseline, candidates 8 and 9, then parked). ATI Mach64 is
   emulator-only. Trio64 hard-locks A8U4I5 on framebuffer readback under
   *any* driver, including S3's own. Text acceleration hangs the physical
   Trio64. A video that only shows wins reads as an advert.

10. **86Box gets things wrong too.** 86Box's ViRGE ignores depth-write
    disable; the emulated Trio64 has a text path the real one does not
    survive. "Virtual vs real" is not "virtual is easy, real is hard", it is
    "each lies about different things".

11. **The DOS box.** Full-screen DOS under a custom driver: one BIOS write of
    02H to 4AE8H traced through the mini-VDD, VDD reservation, the desktop
    doubling on physical Trio64. Nobody making retro videos covers the VDD
    side.

12. **The software Direct3D rasterizer.** `Direct3D=2` in SYSTEM.INI serves
    D3D on the CPU on cards with no 3D at all, including the Trio64. Opt-in
    and slow, but a Trio64 running a D3D title is a good shot.

13. **The 4 MB wall.** "Four megabytes is the resolution limit" — a whole
    class of 1990s constraints in one number.

14. **The S3D 1555-vs-565 mismatch.** The triangle engine writes ZRGB1555
    into a surface described as RGB565, and it was proven by noticing that
    blue is the only colour the two formats agree on. Good for the "S3D"
    segment; it is a puzzle the audience can follow.

15. **Safety design: the poison latch.** Every accelerated GDI path keeps a
    DIB Engine fallback, a bounded wait, and a session-long latch that turns
    acceleration off for good if the engine wedges. Worth 30 seconds because
    it explains why the desktop survives on hardware the project has not
    fully understood.

16. **How to install one of these without bricking the machine.** WININIT.INI
    rename over the agent, RECOVER.TXT, the "back it up cold" warning. Retro
    viewers will try this. Tell them how to get out.

## Points worth trimming or merging

- "Claude" and "Docs Issues/Plans" are one segment: the docs exist in that
  shape *because* of how the sessions were run (handoff docs, decision docs
  as the artefact of a probe loop). Tell it as process, not as a tool plug.
- "S3 good documentation" folds into the Trio vs ViRGE segment. The contrast
  that matters is: databooks are good, the silicon still has two blend
  states the databook does not mention.
- "PCI -> PCIe with Linux Box" and "Linux Probe" are adjacent; run them
  back to back.

## Suggested running order

1. Cold open: the ViRGE claim, one sentence of what you built, the 18 vs 19
   FPS number.
2. What a Win9x driver is (three modules), and the toolchain.
3. The goal and what "better than native" turned out to mean.
4. The test bench: 86Box, QEMU, the remote agent, the Linux probe box and the
   PCIe adapter, the 486, the netbook, A8U4I5.
5. 2D: GDI acceleration, the poison latch, CrystalMark, the text hang.
6. 3D: S3D, the 1555/565 puzzle, the Trio3D blend saga and the retractions.
7. Virtual vs real: where 86Box lied, where the hardware lied.
8. The DOS box.
9. The process: decisions/issues/plans, "measured not asserted", Claude.
10. What does not work, what is next, how to try it without bricking a PC.

## Shots to capture

- Display Properties page showing the driver (already in docs/images).
- Live depth switch 8 -> 16 -> 32 bpp on the ViRGE, no reboot.
- Ironfield side by side, retail vs v9x, same 86Box config.
- The 486 booting Win95 on VLB.
- The netbook desktop with the VBE package.
- A screen recording of a probe run producing an INI, then the decision doc
  quoting it.
- The Trio3D blend A/B: two boots, one variable.
- The retraction banner at the top of a decision doc.

## Facts to double-check before recording

- Exact date range and commit count on the day you record.
- Which card is in A8U4I5 at that point (ViRGE/DX since 6 Sep evening).
- Whether the Matrox path is still parked or was removed.
- Current Direct3D caps gap versus the retail ViRGE driver (README lists
  0x27 vs 0x2F today).
