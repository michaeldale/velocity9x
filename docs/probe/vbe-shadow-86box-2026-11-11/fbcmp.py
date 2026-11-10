"""Compare an 86Box host capture (what the emulated CRTC scans out: the frame
buffer) with the agent's GDI screenshot (what GDI reads: the shadow).

Usage: fbcmp.py host.png agent.bmp [diff.png]

Finds the guest area in the host capture by searching small offsets, then
reports differing pixels and their bounding box. 16 bpp desktops are compared
at 5:6:5 precision, since the two captures expand to 8 bits differently.
"""
import sys
from PIL import Image, ImageChops

host = Image.open(sys.argv[1]).convert("RGB")
agent = Image.open(sys.argv[2]).convert("RGB")
w, h = agent.size


def quant(img):
    return img.point(lambda v: v & 0xF8)


qa = quant(agent)
best = None
for oy in range(60, 90):
    for ox in range(0, 10):
        if ox + w > host.size[0] or oy + h > host.size[1]:
            continue
        crop = quant(host.crop((ox, oy, ox + w, oy + h)))
        # Sample a coarse grid first for speed.
        miss = 0
        for y in range(0, h, 16):
            for x in range(0, w, 16):
                if crop.getpixel((x, y)) != qa.getpixel((x, y)):
                    miss += 1
        if best is None or miss < best[0]:
            best = (miss, ox, oy)

_, ox, oy = best
crop = quant(host.crop((ox, oy, ox + w, oy + h)))
diff = ImageChops.difference(crop, qa).convert("L").point(lambda v: 255 if v else 0)
box = diff.getbbox()
count = sum(1 for v in diff.getdata() if v)
print(f"offset=({ox},{oy}) size={w}x{h} differing_pixels={count} bbox={box}")
if len(sys.argv) > 3:
    diff.save(sys.argv[3])
