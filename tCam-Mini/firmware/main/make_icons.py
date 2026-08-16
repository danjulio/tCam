#!/usr/bin/env python
#
# Generate the web app icons embedded in the firmware.
#
# Writes full-bleed ironbow-gradient PNGs with a spot-meter reticle, at the two
# sizes a web app manifest wants.  Full-bleed matters: these are declared
# "maskable", so the platform crops them to its own shape and any margin we add
# would be cropped twice.  The reticle stays inside the central 80% safe zone.
#
# Pure standard library - no Pillow - so the build has no extra dependency.
# Run from main/:  python make_icons.py
#
# Copyright 2020-2022 Dan Julio
#
# This file is part of tCam.
#
# tCam is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# tCam is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with tCam.  If not, see <https://www.gnu.org/licenses/>.
#
import os
import struct
import sys
import zlib

# Ironbow control points, matching the palette of the same name in the web UI
STOPS = [(0.0, 8, 2, 24), (0.22, 40, 0, 90), (0.45, 140, 20, 110),
         (0.66, 220, 80, 50), (0.85, 255, 180, 20), (1.0, 255, 255, 235)]


def ramp(t):
    for i in range(len(STOPS) - 1):
        a, b = STOPS[i], STOPS[i + 1]
        if a[0] <= t <= b[0]:
            span = b[0] - a[0]
            f = 0.0 if span == 0 else (t - a[0]) / span
            return tuple(int(a[1 + c] + (b[1 + c] - a[1 + c]) * f) for c in range(3))
    return STOPS[-1][1:]


def chunk(tag, data):
    return (struct.pack(">I", len(data)) + tag + data +
            struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))


def make_png(size):
    cx = cy = (size - 1) / 2.0
    r_out = size * 0.30          # reticle radius, inside the 80% safe zone
    ring = max(1.0, size / 48.0)  # stroke width
    gap = r_out * 0.42            # crosshair gap either side of centre

    raw = bytearray()
    for y in range(size):
        raw.append(0)             # filter type 0 (None)
        # Hot at the top, so the icon reads the same way as the UI's scale bar
        base = ramp(1.0 - y / (size - 1.0))
        for x in range(size):
            r, g, b = base
            dx, dy = x - cx, y - cy
            d = (dx * dx + dy * dy) ** 0.5
            on_ring = abs(d - r_out) <= ring
            on_cross = ((abs(dy) <= ring * 0.9 and gap <= abs(dx) <= r_out * 1.5) or
                        (abs(dx) <= ring * 0.9 and gap <= abs(dy) <= r_out * 1.5))
            if on_ring or on_cross:
                # Blend toward white so the reticle reads over any part of the ramp
                r = int(r + (255 - r) * 0.85)
                g = int(g + (255 - g) * 0.85)
                b = int(b + (255 - b) * 0.85)
            raw += bytes((r, g, b))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)   # 8-bit RGB
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
            chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def main():
    here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "www")
    for size in (192, 512):
        path = os.path.join(here, "icon-%d.png" % size)
        data = make_png(size)
        old = None
        if os.path.isfile(path):
            with open(path, "rb") as f:
                old = f.read()
        if old != data:
            with open(path, "wb") as f:
                f.write(data)
        sys.stdout.write("icon-%d.png: %d bytes\n" % (size, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
