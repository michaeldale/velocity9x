# The first Velocity9x driver run on the 486, and where it stopped

Date: 2026-08-21
Branch: `vlb-manual-select-inf`
Machine state at handoff: **486VLB is powered, on the network, and not
reaching the shell.** It needs Michael at the keyboard. See section 4.

Scope: Part B of [the manual-select INF plan](../plans/vlb-manual-select-inf.md).
Part A of that plan is done, green through `run-checks`, and committed.

Related: [the VLB bring-up handoff](2026-08-21-vlb-bringup.md),
[the aperture answer](../decisions/2026-08-21-vlb-aperture-answered.md),
[Win95's own MSDISP.INF](../decisions/2026-08-21-win95-msdisp-486vlb.inf).

---

## 1. What the run proved

**The manual-select model works.** This was the open question Part A existed to
answer, and real Win95 SetupX answered it on the first attempt.

Driven through the remote agent: Display Properties, Settings, Change Display
Type, adapter Change, Have Disk, `C:\V9XPKG`.

* The compatible-devices list Win95 offers for this device is exactly the S3
  801/805/928 set `DETECTS3801` matches on `*PNP0913` - Orchid Fahrenheit,
  Paradise Bahamas, and so on. Confirms the plan's reason for not binding that
  ID: every one of those is a card we have no code for.
* The Have Disk list showed all three of our models, with
  **`Velocity9x S3 (VLB manual select)` first and pre-selected**. An INF model
  line with no hardware ID field is offered on a machine with no PCI bus. That
  is the whole claim of Part A, and it holds on 4.00.950.
* Accepting it rewrote the device: Adapter Type
  `Velocity9x S3 (VLB manual select)`, Manufacturer `Velocity9x`, Version
  `4.0`, Current Files `v9xdisp.drv,*vdd,*vflatd,v9xmini.vxd`. Exactly what
  `[Velocity9x.Registry]` writes. Before the install the same dialog read
  Manufacturer `S3`, Current Files `s3.drv,*vdd,*vflatd,s3.vxd`.

Baseline recorded before touching anything: `C:\V9XHW.INI` and
`C:\V9XBOOT.INI` both absent, desktop up at 800x600x8 on Win95's own
`s3.drv`, `display.drv=pnpdrvr.drv` in `SYSTEM.INI`.

## 2. Where it stopped

The reboot did not come back.

* The agent's `reboot` was accepted and the old connection ended, so the
  machine did restart.
* It has not reconnected. Polled for roughly 25 minutes.
* **ICMP answers, NetBIOS port 139 is open, and the NetBIOS names `486VLB<00>`
  and `486VLB<03>` are registered.** So Windows is running and got far enough
  to bring up the whole TCP/IP and NetBIOS stack.
* Port 9869 is *actively refused* - a RST, not a timeout. The agent is not
  listening.
* No `<20>` server-service name, so there are no file shares to read the disk
  through. The agent is the only channel into this machine and it is the thing
  that is missing.

The agent starts at boot on this machine. Networking up and agent absent
therefore reads as **Windows running with no shell** - the boot got through
ring-0 and net init and stopped at or around the display driver's Enable. That
is consistent with a stage-1 refusal or a failed aperture map, and it is what
the plan's step 6 anticipated. It is not proof: a fully booted desktop with a
crashed agent looks identical from here, and nothing remote can tell them
apart.

**No diagnostics were collected, deliberately.** `C:\V9XBOOT.INI` should now
exist - boot tracing is on by default in this package - and it holds the
furthest lifecycle stage the driver reached. `C:\V9XHW.INI` may exist too. Both
need someone at the machine. Nothing was changed after the failure.

## 3. One thing worth knowing about the session

At 23:36:39, mid-install, another client ran a shell job on this machine:
`dir D:\`. There is no D: drive, so it sat on a DOS *Abort, Retry, Fail?*
prompt until the agent's 125-second timeout killed it, and while it held an
exec slot **every screenshot was refused** with `execution active` -
`v9x_execution_any_active` gates screenshot, power and update on *any* busy
slot. `AGENT.LOG` is what identified it; `V9XWND.EXE` from the package is what
kept the session moving, since it reports the window list without GDI and
without a screenshot.

Two things follow. Assume the 486 is not exclusively yours during a session,
and when screenshots start failing read `C:\V9XREMOTE\AGENT.LOG` before
assuming the guest is wedged.

Also: the guest clock is unreliable. It read 11:39 PM, then 1:45 AM eight
clicks later. Do not use guest timestamps to correlate anything.

## 4. What Michael needs to do, in order

Nothing here is automatable from a session.

1. **Look at the screen.** Whether it is blank, showing a Win95 display-error
   dialog, or showing a working desktop decides everything below. This single
   observation is the whole fork.
2. **If there is no desktop: collect the diagnostics before changing
   anything.** Clean-boot to DOS with **F5, not F8** - F8 with "command prompt
   only" still runs CONFIG.SYS on this machine - and copy off:
   * `C:\V9XBOOT.INI` - the furthest stage the driver reached. This is the
     answer to why it stopped.
   * `C:\V9XHW.INI` - whether `identify_without_pci` fired and what it decided
     the chip was.
   * `C:\BOOTLOG.TXT` - the driver load, from Windows' side.
3. **Then recover.** `C:\V9XPKG\RECOVER.TXT` is on the disk. Its steps say F8;
   use F5 on this machine. Safe Mode, Device Manager, remove the Velocity9x
   display adapter, reboot, let Windows redetect. `C:\V9XPKG\V9XFIX.BAT` is
   also there but expects the package on `D:\ACTIVE`, which is not where it
   was pushed - it is at `C:\V9XPKG`.
4. **If there *is* a desktop**, the driver worked and only the agent died.
   Restart the agent, then finish the plan's step 5 from a session: read
   `C:\V9XHW.INI`, confirm Display Settings offers the 10-mode list, and land
   one mode change.

## 5. What is still open

* The three questions of the plan's step 5 are all still unanswered: whether
  `identify_without_pci` fires, whether `0x7F000000` maps from protected mode,
  and whether a mode set lands. The install is no longer what blocks them.
* The first boot was a **warm restart**, not the cold start `INSTALL.TXT`
  asks for. A cold power cycle needs someone at the machine, and the agent can
  only warm-reboot. If the failure turns out to be state left behind by
  Win95's `s3.drv`, that is the first thing to re-test - cold.
* The schema-2 survey regression on the 86Box PCI targets, unchanged and
  independent of all of this.
