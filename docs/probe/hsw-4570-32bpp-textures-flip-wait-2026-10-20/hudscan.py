import subprocess
import sys

# Counts captured frames where 3DMark's overlay (the time/fps box at the
# bottom right of the 4:3 picture) is missing while the frames either side
# of it carry it: the signature of a half-built buffer on screen. The crop
# is in a 480x270 scaling of the 1920x1080 capture, whose 4:3 picture spans
# x 60..420.
video = sys.argv[1]
w, h = 480, 270
x0, x1, y0, y1 = 240, 420, 255, 270
cmd = ['ffmpeg', '-v', 'error', '-i', video, '-vf',
       'scale=%d:%d,format=gray' % (w, h), '-f', 'rawvideo', '-']
proc = subprocess.Popen(cmd, stdout=subprocess.PIPE)
frame_bytes = w * h
values = []
while True:
    buf = proc.stdout.read(frame_bytes)
    if len(buf) < frame_bytes:
        break
    total = 0
    for y in range(y0, y1):
        row = buf[y * w + x0:y * w + x1]
        total += sum(row)
    values.append(total / ((x1 - x0) * (y1 - y0)))
proc.wait()

drops = []
for i in range(1, len(values) - 1):
    left, here, right = values[i - 1], values[i], values[i + 1]
    if min(left, right) >= 12 and here < 0.6 * min(left, right):
        drops.append(i)
print('frames %d, overlay present (>=12) in %d' %
      (len(values), sum(1 for v in values if v >= 12)))
print('isolated drops %d' % len(drops))
print('first drops (frame, seconds):',
      ' '.join('%d@%.1f' % (d, d / 60.0) for d in drops[:40]))
