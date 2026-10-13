# Voodoo4 found powered down (D3, BARs cleared) under 3dfx's driver

Date: 2026-10-11. Machine: A8U4I5, boot 407. Card: retail Voodoo4 4500 AGP,
3dfx 1.04.00 driver. Status: open, cause not established.

## What was seen

At 19:23:00 an agent screenshot came back as coloured horizontal bands; at
17:27 the same capture had been clean. At 19:23:20 `VSA100.EXE` read the
card's configuration space as:

- command `0000` in the low word (memory and I/O decode off);
- BAR0, BAR1, BAR2 zero, interrupt line zero;
- power-management control (`64h`) = 3, D3;
- every VGA register `FF`.

Config Manager still listed the devnode as started with problem 0 and its
resources allocated (`DYNCM-after-v9xstage.REG`). Captures in
[docs/probe/a8u4i5-voodoo4-velocity9x-2026-10-11](../probe/a8u4i5-voodoo4-velocity9x-2026-10-11/).
A warm restart (boot 408) brought the card back with its earlier BARs and
registers.

## Hypotheses

1. **The Velocity9x preflight.** `V9XSTAGE.EXE` ran 19:21:22-19:22:52 and
   was killed at the agent's 90 s timeout (its closing message box). Not
   reproduced on boot 408: `V9X16LD.EXE /quiet` alone, then the whole
   preflight, left the card in D0 with its BARs.
2. **Monitor power-down.** The active power scheme ("Always On") turns the
   monitor off after 900 s on AC. Before 19:21 the machine had been idle at
   the desktop since about 17:30. A monitor-off that takes a power-managed
   adapter to D3 would fit. Not tested: a 17-minute idle test was started
   and stopped before it read the card.

## Next

Under 3dfx's driver: idle past 15 minutes, then read the card before any
input. Under Velocity9x, whose mini-VDD reports D0-only power capability,
the same test says whether our driver is exposed.
