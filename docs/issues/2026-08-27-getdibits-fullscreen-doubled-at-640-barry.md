# Full-screen GetDIBits capture renders doubled and squeezed at 640x480 on BARRY

Date: 2026-08-27
Status: open, low priority. Found in passing during the GDI-acceleration
root-cause hunt; not related to that defect and not fixed by it.

## Symptom

At 640x480x16 on the physical Trio64, a full-screen BitBlt +
CreateCompatibleBitmap + GetDIBits(24 bpp) capture - the remote agent's
screenshot path - renders the desktop as **two clean side-by-side copies at
half scale in the top half of the image, with the bottom half black**. The
mapping measured against known rectangle positions is content (x, y) appearing
at (x/2, y/2) and again at (x/2 + 320, y/2).

At 800x600x16 the same pipeline is correct (every earlier clean screenshot).
GetPixel at 640x480 is correct: fills verified by GetPixel land where GetPixel
looks, and the desktop displays correctly on the CRT. So scanout and pixel
readback agree with each other; only the full-screen DIB conversion disagrees,
consistent with the conversion walking the frame at double the real pitch.

Oddly, `V9XGDI /probe`'s own full-screen GetDIBits comparison at 640x480
measured a fixed fill exactly (3072/3072 changed pixels at the requested
rectangle, after the acceleration fix), so the artifact may depend on the
destination bitmap or DC specifics rather than hitting every GetDIBits call.

## Where to look

The DIB engine readback / 16-to-24 bpp conversion path for full-screen
captures at 640x480 (pitch 1280), versus 800x600 (pitch 1600). A doubled
pitch (2560) reproduces the observed geometry exactly.

## Impact

Agent screenshots of BARRY at 640x480 are misleading (they cost real time
during the acceleration investigation). No end-user-visible symptom is known.
