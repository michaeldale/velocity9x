# Voodoo4: V9XD32's 32-bit textures draw black

Date: 2026-11-27. Machine: A8U4I5, Voodoo4 4500 PCI (`121A:0009`,
subsystem `121A:0005`), the 0.61.0 tree's `3dfx` set, 800x600x16.
Status: open, cause not looked at. Not a regression: released 0.17.0
(13 October) fails the same way.

## What is seen

V9XD32 `/bpp16` fails one test, T22, in both its forms
([report](../probe/voodoo4-1.0-2026-11-27/voodoo4/v9xd32-bpp16-V9XD32.TXT)):
the A8R8G8B8 and X8R8G8B8 textures are created, locked, given handles
and drawn with every `Hr` 0, but all eight judged pixels read 0
(`T22AOk=0`, `T22XOk=0`, `Failures=2`). T6, the same four-quadrant
texture at 16 bits a texel, passes exactly. Every other V9XD32 test
passes or is skipped for a missing capability (T12 one stage, T18 no
TnL device, T19 no pixel shader).

Released 0.17.0 on the same boot sequence
([report](../probe/voodoo4-1.0-2026-11-27/ab-0.17.0/voodoo4-0170/v9xd32-bpp16-V9XD32.TXT)):
`T22AOk=0`, `T22XOk=0`, `Failures=2`.

3DMark 2000 draws its textures correctly on screen on both builds
(live screenshots), so its content does not depend on these two formats
at 16 bpp, or does not reach them.

The probe is not the cause: T22 passes at 16 bpp on the GCN Radeons
(Carrizo and R3E, `0.47.0-release-checks-2026-11-12` onward). It reads 0
on other older engines too: the Rage XL
([issue](2026-11-26-rage-xl-v9xd32-add-blend-and-32bit-textures.md)),
the Rage IIC, the ViRGE (86Box), the Trio3D/2X and the RIVA 128ZX
(their V9XD32 reports under `docs/probe/`). Whether those share a
cause in the common texture path was not checked.

## To find out

Whether the HAL publishes 32-bit texture formats on the VSA-100, and
what texel format it programs for them (the VSA-100 has ARGB8888);
whether a 32-bit texture uploads with the 16-bit desktop's pitch or
format; whether one cause in the shared texture path covers every
engine that fails T22. Record: [decision](../decisions/2026-11-27-1.0-voodoo4-a8u4i5.md).
