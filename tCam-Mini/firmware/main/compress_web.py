#!/usr/bin/env python
#
# Compress the web UI for embedding in the firmware image.
#
# Invoked from main/CMakeLists.txt at configure time.  Writing the archive with
# mtime=0 keeps the output byte-identical for identical input, so an unchanged UI
# does not produce a different firmware binary on every build.
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
import gzip
import os
import sys


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: compress_web.py <source.html> <dest.html.gz>\n")
        return 1

    src, dst = sys.argv[1], sys.argv[2]

    if not os.path.isfile(src):
        sys.stderr.write("compress_web.py: %s not found\n" % src)
        return 1

    with open(src, "rb") as f:
        raw = f.read()

    payload = gzip.compress(raw, 9) if hasattr(gzip, "compress") else None
    if payload is None:
        import io
        buf = io.BytesIO()
        with gzip.GzipFile(fileobj=buf, mode="wb", compresslevel=9, mtime=0) as g:
            g.write(raw)
        payload = buf.getvalue()
    else:
        # gzip.compress stamps the current time into the header on older Pythons;
        # rewrite the mtime field so repeat builds are reproducible
        payload = payload[:4] + b"\x00\x00\x00\x00" + payload[8:]

    # Avoid touching the file when nothing changed so make does not relink
    if os.path.isfile(dst):
        with open(dst, "rb") as f:
            if f.read() == payload:
                return 0

    with open(dst, "wb") as f:
        f.write(payload)

    sys.stdout.write("web UI: %d -> %d bytes\n" % (len(raw), len(payload)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
