# A8U4I5 stops answering minutes after a Quake 2 session, while idle

Opened 2026-10-03. Machine: A8U4I5 (Rage IIC AGP, Win98SE, Velocity9x HAL
and V9XDISP.DRV), agent at 10.0.1.172:9869.

## What was seen

Twice on 2026-10-03 the machine stopped answering ping and the agent
port within minutes of the last successful agent exchange, both times
while nothing was running on it:

1. Boot 148: Quake 2 (`+set vid_ref gl`, fullscreen 640x480) quit through
   its console, `config.cfg` put back and two logs fetched, all
   successful. About 15 minutes later no ping, no agent. Michael reset it.
2. After the `phase6-px-*` runs: three Quake 2 sessions in a row (640x480
   bilinear, 640x480 `GL_NEAREST`, a 320x240 window), each quit the same
   way, configs restored and logs fetched, all successful. The next
   contact, about 15 minutes later, found no ping and no agent.

Several Quake 2 sessions back to back never lost it; both losses came
after the last session, in idle time.

After the second loss Michael found Windows running normally at the
machine: the desktop worked, so it had not hung or suspended. He did not
test the network from the machine. A normal restart brought the agent
back.

## Not known

Whether the network card, its driver or the TCP/IP stack failed, or only
the agent; whether it happens idle without a Quake 2 session first.
Boot 144's unexplained restart may be related.

## Hypotheses to test

- The network adapter or stack fails (a hung NIC, an interrupt lost after
  heavy agent traffic or a shared IRQ with the display). Test: next time,
  ping out from the machine and look at the adapter in Device Manager
  before restarting.
- Idle power management turning the adapter off. Test: leave the desktop
  idle with no 3D session and poll the agent each minute.
- Something Quake 2's exit leaves armed, firing later.
