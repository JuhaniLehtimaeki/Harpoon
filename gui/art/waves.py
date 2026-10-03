#!/usr/bin/env python3
# Draws the three layers of the sea (waves-back/-middle/-front.svg), white on
# transparent for HighlightImage tinting. Each tiles seamlessly every 1200px;
# render them with:
#   for l in back middle front; do
#     rsvg-convert -w 1200 -h 240 waves-$l.svg -o ../qml/images/waves-$l.png
#   done
import math

W, H, P = 1200, 240, 300

def wave(base, amp, phase, opacity):
    points = " ".join("L%d,%.1f" % (x, base + amp * math.sin(2 * math.pi * x / P + phase))
                      for x in range(0, W + 1, 10))
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d" viewBox="0 0 %d %d">\n'
            '  <path d="M0,%d %s L%d,%d Z" fill="#fff" fill-opacity="%.2f"/>\n'
            '</svg>\n' % (W, H, W, H, H, points, W, H, opacity))

for name, args in (("back", (70, 22, 0.0, 0.30)),
                   ("middle", (120, 18, 1.9, 0.55)),
                   ("front", (170, 14, 3.6, 1.0))):
    with open("waves-%s.svg" % name, "w") as f:
        f.write(wave(*args))
