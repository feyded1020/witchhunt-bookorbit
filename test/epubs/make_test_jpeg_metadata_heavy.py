#!/usr/bin/env python3
"""Build test_jpeg_metadata_heavy.epub -- a JPEG whose SOF marker sits behind a large
metadata block, stored DEFLATED in the archive.

The shape of witchhunt-reader #249. Photoshop/Lightroom exports carry EXIF + IPTC + XMP + ICC
before the frame header; the reporter's book has ~29 KB of it in front of every image. The
image manifest probes the first 4 KB of the entry (kHeaderBufSize) and never reaches SOF, so
the dimensions have to come from a streaming walk of the entry -- an inflate ring the size of
the entry (up to 32 KB), which the C3 cannot always afford mid-parse.

What this fixture pins:
  - photo.jpg is a valid baseline JPEG with a 20 KB APP1 segment right after SOI, so SOF
    lands past the 4 KB probe window. The payload is seeded pseudo-random, so DEFLATE emits
    it as literals: the inflated offset of SOF is what matters, not the compressed size.
  - the entry is DEFLATED (not stored), so reaching SOF needs an inflate ring -- the
    allocation the device fails on. A stored entry could seek for free and would not
    exercise the path at all.
  - the <img> carries no width/height, so the parser must resolve the dimensions itself.
  - 64x48 pixels: small enough that the layout result is trivial and the golden stays
    readable, distinct enough that a 0x0 fallback is unmistakable.

Regenerate with:  python test/epubs/make_test_jpeg_metadata_heavy.py
then refresh goldens: UPDATE_GOLDENS=1 ctest --test-dir build/test/epub_pipeline -R EpubPipeline
"""

import io
import os
import random
import struct
import zipfile

from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_jpeg_metadata_heavy.epub")

WIDTH, HEIGHT = 64, 48
METADATA_BYTES = 20 * 1024  # > kHeaderBufSize (4 KB); the entry-sized ring (~21 KB) then sits between the 8 KB header gate and a 16 KB contig


def make_jpeg():
    # A flat gradient keeps the entropy-coded part tiny; the fixture is about the header.
    img = Image.new("L", (WIDTH, HEIGHT))
    img.putdata([(x * 4) & 0xFF for y in range(HEIGHT) for x in range(WIDTH)])
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=50)
    data = buf.getvalue()
    assert data[:2] == b"\xff\xd8"

    # Seeded, so the fixture is reproducible byte for byte across regenerations.
    rng = random.Random(249)
    payload = b"Exif\x00\x00" + bytes(rng.getrandbits(8) for _ in range(METADATA_BYTES - 6))
    app1 = b"\xff\xe1" + struct.pack(">H", len(payload) + 2) + payload
    # Insert right after SOI: everything the encoder wrote (APP0, DQT, SOF, DHT, SOS) follows.
    return data[:2] + app1 + data[2:]


CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

OPF = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="uid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="uid">test-jpeg-metadata-heavy</dc:identifier>
    <dc:title>JPEG Metadata Heavy</dc:title>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/>
    <item id="photo" href="images/photo.jpg" media-type="image/jpeg"/>
  </manifest>
  <spine>
    <itemref idref="chapter"/>
  </spine>
</package>
"""

CHAPTER = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
  <head>
    <title>Metadata Heavy</title>
  </head>
  <body>
    <h1>Metadata Heavy</h1>
    <p>The paragraph before the photograph must survive.</p>
    <p class="center"><img src="images/photo.jpg" alt="metadata heavy"/></p>
    <p>The paragraph after the photograph must survive.</p>
  </body>
</html>
"""


def main():
    jpeg = make_jpeg()
    sof = jpeg.find(b"\xff\xc0")
    assert sof > 4096, f"SOF at {sof}: must lie beyond the 4 KB probe window"

    # Fixed timestamps: a regenerated fixture with unchanged content stays byte-identical.
    stamp = (2026, 9, 22, 0, 0, 0)

    def add(zf, name, data, method):
        info = zipfile.ZipInfo(name, date_time=stamp)
        info.compress_type = method
        zf.writestr(info, data)

    with zipfile.ZipFile(OUT, "w") as zf:
        add(zf, "mimetype", b"application/epub+zip", zipfile.ZIP_STORED)
        add(zf, "META-INF/container.xml", CONTAINER.encode(), zipfile.ZIP_DEFLATED)
        add(zf, "OEBPS/content.opf", OPF.encode(), zipfile.ZIP_DEFLATED)
        add(zf, "OEBPS/chapter.xhtml", CHAPTER.encode(), zipfile.ZIP_DEFLATED)
        add(zf, "OEBPS/images/photo.jpg", jpeg, zipfile.ZIP_DEFLATED)

    print(f"wrote {OUT}: photo.jpg {len(jpeg)} bytes, SOF at offset {sof}")


if __name__ == "__main__":
    main()
