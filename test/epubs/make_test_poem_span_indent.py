#!/usr/bin/env python3
"""Build test_poem_span_indent.epub — the corpus book that pins a poem drawn with
per-line span indents (Alice's "Mouse's Tale" pattern) inside an inset block.

    <div class="poem">                    .poem { margin-left: 30%; margin-right: 10% }
      <span style="margin-left: 10em">cute you.—</span><br/>

Each line's span margin-left becomes that line's indent. Inside the already inset
poem block the deepest lines no longer fit, and wrapping put the rest of the line
back at the block's left edge, breaking the shape. A span indent now gives way
before the line does: a line whose words fit the column keeps to one line, its
indent shortened by what it lacks. Lines whose words do not fit even without the
indent still wrap (case 3), and a paragraph's own text-indent is untouched
(case 4).

At the corpus font (1em = 18px, 480px viewport) the poem column is 360px wide
(30% capped at 4em = 72px on the left, 10% = 48px on the right), so:

  1. short lines       fit with their indent: x = the indent
  2. long lines        fit only without part of it: one line each, x shortened
  3. a line too long   for the column: wraps, as before
  4. text-indent       a paragraph's own indent never yields

Regenerate with:  python test/epubs/make_test_poem_span_indent.py
then refresh goldens: UPDATE_GOLDENS=1 ctest -R EpubPipeline
"""

import os
import zipfile

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_poem_span_indent.epub")

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
    <dc:title>Poem Span Indent Test</dc:title>
    <dc:creator>Test Suite</dc:creator>
    <dc:identifier id="BookId">urn:uuid:test-poem-span-indent-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="ncx"      href="toc.ncx"        media-type="application/x-dtbncx+xml"/>
    <item id="css"      href="style.css"      media-type="text/css"/>
    <item id="chapter1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
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
    <meta name="dtb:uid" content="urn:uuid:test-poem-span-indent-001"/>
    <meta name="dtb:depth" content="1"/>
    <meta name="dtb:totalPageCount" content="0"/>
    <meta name="dtb:maxPageNumber" content="0"/>
  </head>
  <docTitle><text>Poem Span Indent Test</text></docTitle>
  <navMap>
    <navPoint id="np1" playOrder="1">
      <navLabel><text>Poem Span Indent Test</text></navLabel>
      <content src="chapter1.xhtml"/>
    </navPoint>
  </navMap>
</ncx>
"""

CSS = """body { margin: 0 }
p { margin: 0 }
.poem { margin-left: 30%; margin-right: 10%; text-align: left }
p.indented { text-indent: 12em }
"""

LINES = [
    # (indent em, text)
    (0, "L01 Fury said to"),
    (2, "L02 a mouse, That"),
    (4, "L03 he met in the"),
    (6, "L04 house, Let us both go"),
    (8, "L05 to law: I will prose-"),
    (10, "L06 cute you. Come, I will take"),
    (12, "L07 no denial: We must have the"),
    (10, "L08 trial; For really"),
    (6, "L09 this morning I have nothing to do."),
    (8, "L10 Said the mouse to the cur, Such a trial, dear sir, with no jury or judge"),
    (2, "L11 would be wasting our breath."),
]


def chapter():
    poem = "\n".join(
        (f'<span style="margin-left: {em}em;">{text}</span><br/>' if em else f"{text}<br/>") for em, text in LINES
    )
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>Poem</title><link rel="stylesheet" type="text/css" href="style.css"/></head>
<body>
<p>BEFORE the tale.</p>
<div class="poem">
{poem}
</div>
<p class="indented">INDENTED A paragraph whose own text-indent pushes its first line far in, and
which simply wraps.</p>
</body>
</html>
"""


def main():
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", OPF, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/toc.ncx", NCX, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/style.css", CSS, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/chapter1.xhtml", chapter(), compress_type=zipfile.ZIP_DEFLATED)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
