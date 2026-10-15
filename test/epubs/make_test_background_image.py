#!/usr/bin/env python3
"""Build test_background_image.epub — the corpus book that pins how CSS background
pictures reach the page.

The renderer cannot draw text over a picture, so a container's no-repeat
background picture becomes a picture of its own, placed after the container's
content (Gutenberg's illustrated Alice draws its rabbit hole as the background
of a table, with 22 <br/> at the end of the cell to make room for it). Inside
such a container a run of <br/> is cut to two.

Cases, in order:

  1. table.picture   one-cell table, url relative to the stylesheet's own
                     directory (../images/), trailing <br/> run: the grid,
                     then the picture
  2. div.boxed       text and five <br/>: the text, then the picture -- the run
                     is cut to two, and those leave no gap before a picture
  3. inline style    <p style="background: url(...) no-repeat">: the picture
                     after the paragraph
  4. div.ornament    a 40x40 background: decoration, no picture
  5. div.tiled       a large background that repeats: a texture, no picture
  6. li.bullet       a list bullet image: no picture
  7. body            a page background: no picture

Regenerate with:  python test/epubs/make_test_background_image.py
then refresh goldens: UPDATE_GOLDENS=1 ctest -R EpubPipeline
"""

import os
import struct
import zipfile
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_background_image.epub")


def png(width, height):
    """A grey diagonal-stripe PNG, 8-bit greyscale, no external dependencies."""
    rows = b"".join(b"\x00" + bytes(((x + y) // 8 % 2) * 160 + 40 for x in range(width)) for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    header = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

OPF = """<?xml version="1.0" encoding="UTF-8"?>
<package version="2.0" xmlns="http://www.idpf.org/2007/opf" unique-identifier="BookId">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:opf="http://www.idpf.org/2007/opf">
    <dc:title>Background Image Test</dc:title>
    <dc:creator>Test Suite</dc:creator>
    <dc:identifier id="BookId">urn:uuid:test-background-image-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="ncx"      href="toc.ncx"             media-type="application/x-dtbncx+xml"/>
    <item id="css"      href="css/style.css"       media-type="text/css"/>
    <item id="hole"     href="images/hole.png"     media-type="image/png"/>
    <item id="tall"     href="images/tall.png"     media-type="image/png"/>
    <item id="orn"      href="images/ornament.png" media-type="image/png"/>
    <item id="chapter1" href="text/chapter1.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine toc="ncx">
    <itemref idref="chapter1"/>
  </spine>
</package>
"""

NCX = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE ncx PUBLIC "-//NISO//DTD ncx 2005-1//EN" "http://www.daisy.org/z3986/2005/ncx-2005-1.dtd">
<ncx version="2005-1" xmlns="http://www.daisy.org/z3986/2005/ncx/">
  <head>
    <meta name="dtb:uid" content="urn:uuid:test-background-image-001"/>
    <meta name="dtb:depth" content="1"/>
    <meta name="dtb:totalPageCount" content="0"/>
    <meta name="dtb:maxPageNumber" content="0"/>
  </head>
  <docTitle><text>Background Image Test</text></docTitle>
  <navMap>
    <navPoint id="np1" playOrder="1">
      <navLabel><text>Background Image Test</text></navLabel>
      <content src="text/chapter1.xhtml"/>
    </navPoint>
  </navMap>
</ncx>
"""

CSS = """body { margin: 0; background: url(../images/tall.png) no-repeat }
p { margin: 0 }
table.picture { width: 600px; background-image: url(../images/hole.png); background-repeat: no-repeat }
div.boxed { background: url("../images/tall.png") no-repeat right top }
div.ornament { background: url(../images/ornament.png) no-repeat }
div.tiled { background-image: url(../images/tall.png) }
li.bullet { background: url(../images/hole.png) no-repeat left center; list-style: none }
"""

CHAPTER = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>Background Image</title><link rel="stylesheet" type="text/css" href="../css/style.css"/></head>
<body>
<p>CASE1 A table whose picture is its background.</p>
<table class="picture"><tr><td>CELL Either the well was very deep, or she fell very slowly.<br/>
<br/>
<br/>
<br/>
<br/>
<br/>
</td></tr></table>
<p>CASE2 A boxed passage with room left for its picture.</p>
<div class="boxed">BOXED The text of the box.<br/>
<br/>
<br/>
<br/>
<br/>
</div>
<p>CASE3 BEFORE</p>
<p style="background: url(../images/hole.png) no-repeat">INLINE A paragraph with an inline background.</p>
<div class="ornament">CASE4 ORNAMENT A small flourish behind this line.</div>
<div class="tiled">CASE5 TILED Paper texture behind this line.</div>
<ul><li class="bullet">CASE6 BULLET An item with an image bullet.</li></ul>
<p>CASE7 END The body background is never a picture.</p>
</body>
</html>
"""


def main():
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", OPF, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/toc.ncx", NCX, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/css/style.css", CSS, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/images/hole.png", png(200, 260), compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/images/tall.png", png(160, 240), compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/images/ornament.png", png(40, 40), compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/text/chapter1.xhtml", CHAPTER, compress_type=zipfile.ZIP_DEFLATED)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
