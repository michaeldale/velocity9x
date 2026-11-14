# Netbook stops at boot 325 with KeBugCheckEx FE (BUGCODE_USB_DRIVER), at an idle desktop

Date: 2026-11-13. Status: open, not reproduced. The second time on this
machine; the evidence points at the USB stack, not Velocity9x, without
excluding it.

Machine: MICHAEL-NETBOOK (W9QI2BE2, HP Mini, 945GSE / GMA 950), Windows
98 SE, at 10.0.1.6 through a NETGEAR WG111v2 USB wifi adapter.

## What was seen

Michael, on the panel, before the 0.49.0 release checks; the machine
needed a hard power-off:

```
KEBUGCHECKEX:  called by ff030db3
Error code is fe
Parameters: 2 c1aa41d0 c1aa0eb0 c1aa1570

System halted.
```

## What the agent log shows

`C:\V9XREMOTE\AGENT.LOG`: boot 325 started the agent at 18:20:39
(`agent-start`, `listening`, `mdns-active`, tray icon at 10.0.1.6), then
nothing: no client connected and no command ran. Boot 326's agent started
at 18:24:43. So the stop came within about four minutes of the desktop,
with no 3D, DirectDraw or probe program started by us.

The driver set was the one installed at boot 324 (746c0a42's code, run as
4146a0f0-dirty, `193e311d`): the Gen3 texture pool was mapped.

## Reading

- Code `FE` is BUGCODE_USB_DRIVER. Parameter 1 = 2 is, in Microsoft's
  bug check reference, a USB client driver submitting a URB that is still
  attached to another IRP pending in the bus driver; parameters 2 to 4 are
  the pending IRP, the IRP just passed in, and the URB. Win98 raises it
  through NTKERN. Those three addresses (`C1AAxxxx`) are in the system
  arena, where IRPs live.
- USB clients on this machine (`inventory`, boot 327): the WG111v2
  (`USB\VID_0846&PID_6A00`, NETGEAR driver dated 2006), and a Sierra
  Wireless composite device (`USB\VID_1199&PID_68A3`, the built-in WWAN
  module) at problem 10, failed to start. The EHCI controller, USB 2.0
  root hub and composite driver are dated 2026-09-18, a later stack than
  Win98's own.
- Velocity9x has no USB code. The one way it could take part is a stray
  write into another driver's memory; nothing here shows one.
- The same code stopped this machine once before, at boot 238
  (2026-10-25, on 1c3490eb), before the texture pool existed. Its
  parameters were not recorded.

Not established: which driver called KeBugCheckEx (`FF030DB3` was not
mapped to a module), and whether the pool can be excluded.

## Next

- If it recurs, photograph the screen again and note what was plugged in
  and whether the wifi had connected.
- Candidate test, needs Michael: disable the failed Sierra composite
  device in Device Manager, or run on the AR8132 ethernet with the WG111v2
  unplugged, and count boots without a stop.
