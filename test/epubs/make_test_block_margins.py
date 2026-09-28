#!/usr/bin/env python3
"""Build test_block_margins.epub — the corpus book that pins where a block
element's bottom spacing lands.

A block element's style starts only its FIRST text block: child blocks and
<br> lines follow it, and a wrapper's empty block is merged into its first
child. Bottom spacing left on that style therefore landed under the first
child instead of after the element:

    div.wrap { margin-bottom: 1em }     <div class="wrap"><p>a</p><p>b</p></div>

opened the gap between a and b. The cases below cover each shape:

  1. wrapper around two paragraphs  the gap follows the second paragraph
  2. heading split by <br>          the gap follows the whole heading, not its
                                    first line
  3. paragraph split by <br>        the same for a paragraph
  4. empty spacer div               its margin-bottom opens the next paragraph,
                                    collapsing with that paragraph's margin-top
  5. nested wrappers, one end tag   their margins collapse to the larger
     after the other
  6. padding-bottom                 added after the last child, like margin
  7. figure with a caption          the wrapper's margin follows the caption,
                                    not the image
  8. figure without a caption       the margin still separates the image from
                                    the next paragraph
  9. item holding a nested list     the item's margin follows the nested list
 10. <section> around paragraphs    a container that is not a BLOCK_TAG still
                                    opens a gap before its first child and
                                    after its last, not between them
 11. <aside> holding bare text      its text gets a block of its own, spaced
                                    like any other
 12. sibling after the cases        no spacing left over

Label paragraphs ("1. WRAPPER:") carry no margins, so every gap in the golden
is the case's own. At the corpus font 1em = 18px and a body line is 24px, so
an 18px gap shows as a 42px step between LINE y values, a 9px gap as 33px.

Regenerate with:  python test/epubs/make_test_block_margins.py
then refresh goldens: UPDATE_GOLDENS=1 ctest -R EpubPipeline
"""

import os
import struct
import zipfile
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_block_margins.epub")

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
    <dc:title>Block Margins Test</dc:title>
    <dc:creator>Test Suite</dc:creator>
    <dc:identifier id="BookId">urn:uuid:test-block-margins-001</dc:identifier>
    <dc:language>en</dc:language>
  </metadata>

  <manifest>
    <item id="ncx"      href="toc.ncx"        media-type="application/x-dtbncx+xml"/>
    <item id="css"      href="style.css"      media-type="text/css"/>
    <item id="fig"      href="fig.png"        media-type="image/png"/>
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
    <meta name="dtb:uid" content="urn:uuid:test-block-margins-001"/>
    <meta name="dtb:depth" content="1"/>
    <meta name="dtb:totalPageCount" content="0"/>
    <meta name="dtb:maxPageNumber" content="0"/>
  </head>
  <docTitle><text>Block Margins Test</text></docTitle>
  <navMap>
    <navPoint id="np1" playOrder="1">
      <navLabel><text>Block Margins Test</text></navLabel>
      <content src="chapter1.xhtml"/>
    </navPoint>
  </navMap>
</ncx>
"""

CSS = """/* No book-level spacing: every gap below comes from the case's own CSS. */
body { margin: 0; padding: 0 }
p { margin: 0; text-indent: 0 }
h2 { margin: 0 }

/* 1. */ div.wrap { margin-bottom: 1em }
/* 2. */ h2.two { margin-bottom: 1em }
/* 3. */ p.br { margin-bottom: 1em }
/* 4. */ div.spacer { margin-bottom: 1em }
         p.after { margin-top: 0.5em }
/* 5. */ div.outer { margin-bottom: 1em }
         div.inner { margin-bottom: 0.5em }
/* 6. */ div.pad { padding-bottom: 1em }
/* 7. */ div.fig { margin-bottom: 1em }
/* 8. */ div.figonly { margin: 1em 0 }
/* 9. */ li.item { margin-bottom: 1em }
/* 10. */ section.sec { margin: 1em 0 }
/* 11. */ aside.note { margin: 0.5em 0 }
"""

CHAPTER = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head>
  <title>Block Margins Test</title>
  <link rel="stylesheet" type="text/css" href="style.css"/>
</head>
<body>

  <p>1. WRAPPER:</p>
  <div class="wrap">
    <p>Alpha first</p>
    <p>Alpha second</p>
  </div>

  <p>2. HEADING:</p>
  <h2 class="two">Bravo top<br/>Bravo bottom</h2>

  <p>3. BR PARAGRAPH:</p>
  <p class="br">Charlie top<br/>Charlie bottom</p>

  <p>4. SPACER:</p>
  <div class="spacer"></div>
  <p class="after">Delta after the spacer</p>

  <p>5. NESTED:</p>
  <div class="outer"><div class="inner"><p>Echo inner</p></div></div>

  <p>6. PADDING:</p>
  <div class="pad">
    <p>Foxtrot first</p>
    <p>Foxtrot second</p>
  </div>

  <p>7. FIGURE:</p>
  <div class="fig">
    <img src="fig.png" alt="" width="40" height="40"/>
    <p>Golf caption</p>
  </div>

  <p>8. FIGURE ONLY:</p>
  <div class="figonly"><img src="fig.png" alt="" width="40" height="40"/></div>

  <p>9. ITEM:</p>
  <ul>
    <li class="item">Hotel outer
      <ul><li>Hotel inner</li></ul>
    </li>
    <li>Hotel next</li>
  </ul>

  <p>10. SECTION:</p>
  <section class="sec">
    <p>India first</p>
    <p>India second</p>
  </section>

  <p>11. ASIDE:</p>
  <aside class="note">Juliet bare text</aside>

  <p>12. SIBLING: no spacing left over.</p>

</body>
</html>
"""


def png_gray(width, height):
    """A tiny 8-bit greyscale PNG (diagonal gradient) -- enough for the header read and a dither."""
    rows = []
    for y in range(height):
        row = bytes(((x * 255) // (width - 1) + y * 2) & 0xFF for x in range(width))
        rows.append(b"\x00" + row)  # filter type 0 per scanline
    raw = b"".join(rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)  # 8-bit, greyscale, no interlace
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    if os.path.exists(OUT):
        os.remove(OUT)
    with zipfile.ZipFile(OUT, "w") as z:
        # mimetype must be first and stored uncompressed
        z.writestr("mimetype", "application/epub+zip", zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", OPF, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/toc.ncx", NCX, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/style.css", CSS, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/fig.png", png_gray(40, 40), zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/chapter1.xhtml", CHAPTER, zipfile.ZIP_DEFLATED)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
