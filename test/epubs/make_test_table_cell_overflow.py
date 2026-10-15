#!/usr/bin/env python3
"""Build test_table_cell_overflow.epub — the corpus book that pins a table row
falling back to paragraphs across page boundaries.

A table cell taller than the viewport can never be a grid row, so the parser
replays the row's buffered cells as ordinary paragraphs. On an arena build
(the reader's background and foreground builds, and EpubPipelineTestArena)
those replayed lines used to keep the wiring of a grid row: lines in the
arena with no page-fit hook. A line that did not fit was then built inside
the current page's arena block before the page was emitted, the emit rewound
the block under it, and the following lines overwrote it -- the next page was
written with a corrupt TextBlock and could not be read (X3, Alice's chapter 2,
a cell holding a tall picture).

Three cases, each a four-column table whose first cell is too long for a grid
cell, preceded by an intro of a different length so the replay crosses a page
boundary at a different point, and followed by a long paragraph that fills the
page it crossed into. Each sentence is numbered, which makes a lost or garbled
line visible in the golden.

Regenerate with:  python test/epubs/make_test_table_cell_overflow.py
then refresh goldens: UPDATE_GOLDENS=1 ctest -R EpubPipeline
"""

import os
import zipfile

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_table_cell_overflow.epub")

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

OPF = """<?xml version="1.0" encoding="UTF-8"?>
<package version="2.0"
         xmlns="http://www.idpf.org/2007/opf"
         unique-identifier="BookId">

  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/"
            xmlns:opf="http://www.idpf.org/2007/opf">
    <dc:title>Table Cell Overflow Test</dc:title>
    <dc:creator>Test Suite</dc:creator>
    <dc:identifier id="BookId">urn:uuid:test-table-cell-overflow-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>

  <manifest>
    <item id="ncx"      href="toc.ncx"        media-type="application/x-dtbncx+xml"/>
    <item id="chapter1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
  </manifest>

  <spine toc="ncx">
    <itemref idref="chapter1"/>
  </spine>

</package>
"""

NCX = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE ncx PUBLIC "-//NISO//DTD ncx 2005-1//EN"
  "http://www.daisy.org/z3986/2005/ncx-2005-1.dtd">
<ncx version="2005-1" xmlns="http://www.daisy.org/z3986/2005/ncx/">
  <head>
    <meta name="dtb:uid" content="urn:uuid:test-table-cell-overflow-001"/>
    <meta name="dtb:depth" content="1"/>
    <meta name="dtb:totalPageCount" content="0"/>
    <meta name="dtb:maxPageNumber" content="0"/>
  </head>
  <docTitle><text>Table Cell Overflow Test</text></docTitle>
  <navMap>
    <navPoint id="np1" playOrder="1">
      <navLabel><text>Table Cell Overflow Test</text></navLabel>
      <content src="chapter1.xhtml"/>
    </navPoint>
  </navMap>
</ncx>
"""

WORDS = ("the reader turned another page while the rain kept falling on the old roof and "
         "somewhere below a door closed softly").split()


def sentence(n):
    # Deterministic, varied word counts so line breaks fall at different places.
    count = 8 + (n * 7) % 11
    body = " ".join(WORDS[(n + i) % len(WORDS)] for i in range(count))
    return f"S{n:03d} {body.capitalize()}."


def sentences(first, count):
    return " ".join(sentence(n) for n in range(first, first + count))


def case(index, intro_sentences):
    # Four columns make the first cell narrow: ~180 words need more lines than a grid cell may
    # hold (MAX_CELL_LINES) while the row stays under the parser's row buffer budget, so the row
    # is laid out, refused and replayed as paragraphs. The intro decides where on the page the
    # replay starts; the paragraph after it fills the page the replay crossed into, which is what
    # overwrote the moved line.
    base = index * 1000
    return f"""<p>INTRO{index} {sentences(base + 1, intro_sentences)}</p>
<table>
  <tr><td>CELL{index} {sentences(base + 100, 16)}</td><td>B{index}</td><td>C{index}</td><td>D{index}</td></tr>
</table>
<p>AFTER{index} {sentences(base + 300, 40)}</p>
"""


def chapter():
    cases = "".join(case(i, intro) for i, intro in enumerate((14, 22, 30), start=1))
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml">
<head><title>Table Cell Overflow</title></head>
<body>
{cases}</body>
</html>
"""


def main():
    with zipfile.ZipFile(OUT, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", OPF, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/toc.ncx", NCX, compress_type=zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/chapter1.xhtml", chapter(), compress_type=zipfile.ZIP_DEFLATED)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
