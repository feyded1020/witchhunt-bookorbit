#!/usr/bin/env python3
"""Build test_lists.epub — the corpus book that pins where list items start
horizontally, and which marker they carry.

In CSS a list item's text starts at the list's content edge: the <ul>/<ol>'s
own margin-left and padding-left, plus the <li>'s own margin-left. Browsers
give the list `padding-inline-start: 40px` by default; the firmware's
equivalent is 1.5em. Publishers state the indent on either element, and the
shape that broke (Strange Pictures, chapter 1) puts it on the list and zeroes
the item:

    ul.bullet { margin-left: 2.5em; padding-left: 0 }    li.bull { margin-left: 0 }

The cases below cover each place the indent can come from:

  1. indent on the list          the reported shape: the list's margin must
                                 reach every item even though the item says 0,
                                 and its 0.8em top and bottom margins must open
                                 a gap before the first item and after the last
  2. no CSS at all               the default list indent
  3. list margin, no padding     the margin adds to the default padding
  4. list reset to zero          margin:0 + padding:0 puts items at the edge
  5. indent on the item          the list zeroed, the item carrying the inset
  6. nested lists                each level adds its own indent
  7. ordered list, start=3       counter and marker; same default indent
  8. paragraphs inside <li>      every block in the item keeps the list inset,
                                 and the marker opens the first paragraph
                                 instead of sitting on a line of its own
  9. a wrapping item             its continuation lines keep the inset too
 10. anchor before the <p>       an empty inline element does not strand the
                                 marker either
 11. an empty item               still shows its marker, which must not leak
                                 into the next item
 12. an item opening with a list the outer marker keeps a line of its own
 13. list and item margins       the list's margin collapses with the first
                                 item's margin-top and the last item's
                                 margin-bottom; between items only the items'
 14. list ends in an empty item  nothing is left to carry the list's
                                 margin-bottom, so it opens the next paragraph
 15. sibling after the lists     back at the text edge

At the corpus font 1em = 18px, so the goldens read: case 1 at LINE x=45 (and
18px right inset), case 2 at 27, case 3 at 45, case 4 at 0, case 5 at 36,
case 6 at 27 then 54, case 7 at 27, case 8 at 27 with the marker in front of
the first paragraph, case 9 at 27 on every line, case 10 like case 8, case 11
a lone marker then a marked item, case 12 a lone marker at 27 then the inner
item at 54, case 13 gaps of 18 / 9 / 18 around and between its items (not
27), case 14 a gap of 18 before the next paragraph, case 15 back at 0.

Regenerate with:  python test/epubs/make_test_lists.py
then refresh goldens: UPDATE_GOLDENS=1 ctest -R EpubPipeline
"""

import os
import zipfile

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_lists.epub")

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
    <dc:title>List Test</dc:title>
    <dc:creator>Test Suite</dc:creator>
    <dc:identifier id="BookId">urn:uuid:test-lists-001</dc:identifier>
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
<!DOCTYPE ncx PUBLIC "-//NISO//DTD ncx 2005-1//EN"
  "http://www.daisy.org/z3986/2005/ncx-2005-1.dtd">
<ncx version="2005-1" xmlns="http://www.daisy.org/z3986/2005/ncx/">
  <head>
    <meta name="dtb:uid" content="urn:uuid:test-lists-001"/>
    <meta name="dtb:depth" content="1"/>
    <meta name="dtb:totalPageCount" content="0"/>
    <meta name="dtb:maxPageNumber" content="0"/>
  </head>
  <docTitle><text>List Test</text></docTitle>
  <navMap>
    <navPoint id="np1" playOrder="1">
      <navLabel><text>List Test</text></navLabel>
      <content src="chapter1.xhtml"/>
    </navPoint>
  </navMap>
</ncx>
"""

CSS = """/* No book-level insets: every x below comes from the case's own CSS. */
body { margin: 0; padding: 0 }
p { margin: 0 }

/* 1. Strange Pictures: the indent on the list, the item explicitly zeroed. */
ul.bullet { list-style-type: disc; margin: 0.8em 1em 0.8em 2.5em; padding-left: 0 }
li.bull { margin-left: 0; text-indent: 0 }

/* 3. A list margin with no padding stated: the default padding still applies. */
ul.margin { margin-left: 1em }

/* 4. A list reset to the text edge. */
ul.flat { margin: 0; padding: 0; list-style: none }

/* 5. The list zeroed, the inset on the item. */
ul.zero { margin-left: 0; padding-left: 0 }
li.ind { margin-left: 2em }

/* 13. List margins and item margins: adjacent vertical margins collapse. */
ul.gap { margin: 1em 0 }
li.gap { margin: 0.5em 0 }

/* 14. The same list, unmarked, ending in an empty item. */
ul.gapflat { margin: 1em 0; list-style: none }
"""

CHAPTER = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN"
  "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en">
<head>
  <title>List Test</title>
  <link rel="stylesheet" type="text/css" href="style.css"/>
</head>
<body>

  <h1>List Test</h1>

  <!-- 1. Indent on the list, item zeroed (Strange Pictures) ──────────────── -->
  <p>1. ON THE LIST:</p>
  <ul class="bullet">
    <li class="bull">Alpha one</li>
    <li class="bull">Alpha two</li>
  </ul>

  <!-- 2. No CSS ───────────────────────────────────────────────────────────── -->
  <p>2. DEFAULT:</p>
  <ul>
    <li>Bravo one</li>
    <li>Bravo two</li>
  </ul>

  <!-- 3. List margin, no padding ──────────────────────────────────────────── -->
  <p>3. LIST MARGIN:</p>
  <ul class="margin">
    <li>Charlie one</li>
  </ul>

  <!-- 4. Reset to the edge ────────────────────────────────────────────────── -->
  <p>4. FLAT:</p>
  <ul class="flat">
    <li>Delta one</li>
  </ul>

  <!-- 5. Indent on the item ───────────────────────────────────────────────── -->
  <p>5. ON THE ITEM:</p>
  <ul class="zero">
    <li class="ind">Echo one</li>
  </ul>

  <!-- 6. Nested ───────────────────────────────────────────────────────────── -->
  <p>6. NESTED:</p>
  <ul>
    <li>Foxtrot outer
      <ul>
        <li>Foxtrot inner</li>
      </ul>
    </li>
    <li>Foxtrot after</li>
  </ul>

  <!-- 7. Ordered, start=3 ─────────────────────────────────────────────────── -->
  <p>7. ORDERED:</p>
  <ol start="3">
    <li>Golf three</li>
    <li>Golf four</li>
  </ol>

  <!-- 8. Paragraphs inside an item ────────────────────────────────────────── -->
  <p>8. PARAGRAPHS:</p>
  <ul>
    <li><p>Hotel first paragraph</p><p>Hotel second paragraph</p></li>
  </ul>

  <!-- 9. A wrapping item ──────────────────────────────────────────────────── -->
  <p>9. WRAPPING:</p>
  <ul>
    <li>India is a list item long enough to wrap onto a second line, and every
    line of it has to start at the same list inset as the first.</li>
  </ul>

  <!-- 10. Anchor before the paragraph ───────────────────────────────────── -->
  <p>10. ANCHORED:</p>
  <ul>
    <li><a id="juliet"/><p>Juliet anchored paragraph</p></li>
  </ul>

  <!-- 11. An empty item ─────────────────────────────────────────────────── -->
  <p>11. EMPTY ITEM:</p>
  <ul>
    <li></li>
    <li>Kilo two</li>
  </ul>

  <!-- 12. An item opening with a list ───────────────────────────────────── -->
  <p>12. OPENS WITH A LIST:</p>
  <ul>
    <li><ul><li>Lima inner</li></ul></li>
  </ul>

  <!-- 13. List and item margins ──────────────────────────────────────────── -->
  <p>13. MARGINS:</p>
  <ul class="gap">
    <li class="gap">Mike one</li>
    <li class="gap">Mike two</li>
  </ul>

  <!-- 14. A list ending in an empty item ────────────────────────────────── -->
  <p>14. EMPTY LAST ITEM:</p>
  <ul class="gapflat">
    <li>November one</li>
    <li></li>
  </ul>

  <!-- 15. After the lists ─────────────────────────────────────────────────── -->
  <p>15. SIBLING: back at the text edge.</p>

</body>
</html>
"""


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
        z.writestr("OEBPS/chapter1.xhtml", CHAPTER, zipfile.ZIP_DEFLATED)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
