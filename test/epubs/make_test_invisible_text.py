#!/usr/bin/env python3
"""Build test_invisible_text.epub -- text made invisible by CSS rather than removed by it.

test_display_none.epub and test_hidden_attr.epub cover text that is REMOVED. This one covers
text that is still in the flow but cannot be seen, which the parser rendered as if it were
ordinary text. The motivating book is a PDF-to-EPUB conversion (Deckhand): a full-page scan
per chapter, then every OCR word as its own absolutely positioned <div class="t"
style="...;color:transparent;">. To us that was one page of picture followed by eleven pages
of single words -- 96% of the book's lines carried one word, and the "154-page" book paginated
to 1,206.

Two mechanisms, because the flags do not all mean the same thing:
  text-only skip (images unaffected):  color: transparent, -webkit-text-fill-color: transparent,
                                       and alpha-zero colours: rgba(...,0), hsla(...,0), #rrggbb00
  subtree skip (images included):      opacity: 0, visibility: hidden | collapse

What each chapter pins:
  1. the Deckhand shape exactly        -- scan image survives, twenty OCR runs vanish, caption stays
  2. transparent via a stylesheet class -- the rule route, not only the inline route
  3. alpha-zero colours                 -- rgba / hsla / 8-digit hex; opaque rgba and 6-digit
                                           hex (#000000 ends in "00" too) must survive
  4. -webkit-text-fill-color            -- the Safari-era spelling some exports use
  5. visibility: hidden / collapse      -- a hidden container hides its IMAGE as well as its text
  6. opacity: 0                         -- likewise; opacity 0.5 is dim, not invisible, and stays
  7. explicit colours                   -- red is not transparency; a later `color: black` in the
                                           same style attribute wins over an earlier transparent;
                                           the keyword is case-insensitive
  8. a visible control chapter          -- nothing here may disappear

Every ghost carries a unique UPPERCASE marker so a golden that leaks one is a regression rather
than a pass, and every chapter carries visible text so a golden that loses the visible half is
too. Goldens only, per corpus convention; the markers make a wrong golden reviewable.

Regenerate with:  python test/epubs/make_test_invisible_text.py
then refresh goldens: UPDATE_GOLDENS=1 ctest --test-dir test/build -R EpubPipeline
"""

import os
import struct
import zipfile
import zlib

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "test_invisible_text.epub")

CSS = """.pagefull { margin: 0; padding: 0; text-align: center; }
.pageimg { max-width: 100%; height: auto; display: block; margin: 0 auto; }
.page { position: relative; overflow: hidden; margin: 0 auto; }
.t { position: absolute; white-space: pre; line-height: 1; }
.ocr { color: transparent; }
.fill { -webkit-text-fill-color: transparent; }
.ghost { visibility: hidden; }
.vanish { opacity: 0; }
.red { color: red; }
"""

CHAPTER = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
  <head>
    <title>{title}</title>
    <link rel="stylesheet" type="text/css" href="style.css"/>
  </head>
  <body>
    <h1>{title}</h1>
{body}
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


def deckhand_runs(count):
    """Twenty OCR runs in the exact shape the converter emits: one word per absolutely positioned div."""
    lines = []
    for i in range(1, count + 1):
        left = 96 + (i % 4) * 60
        top = 16 + i * 14
        lines.append(
            f'      <div class="t" style="left:{left}px;top:{top}.49px;font-size:9.33px;'
            f"font-family:'epubf0', 'GlyphLessFont';color:transparent;\">OCRWORD{i:02d}</div>"
        )
    return "\n".join(lines)


CHAPTERS = [
    (
        "Fixed Layout Scan",
        '    <div class="pagefull"><img class="pageimg" src="img/page.png" alt=""/></div>\n'
        '    <div class="page" style="width:381.40px;height:598.80px">\n'
        + deckhand_runs(20)
        + "\n    </div>\n"
        "    <p>VISIBLECAPTION the caption under the scan must survive.</p>",
    ),
    (
        "Stylesheet Transparent",
        '    <p class="ocr">STYLESHEETGHOST hidden by a class rule, not an inline style.</p>\n'
        "    <p>VISIBLETWO the paragraph after it must survive.</p>",
    ),
    (
        "Alpha Zero Colours",
        '    <p style="color: rgba(0,0,0,0)">RGBAGHOST alpha zero, compact spelling.</p>\n'
        '    <p style="color: rgba( 0 , 0 , 0 , 0.0 )">RGBAGHOSTTWO alpha zero, spaces and a decimal.</p>\n'
        '    <p style="color: hsla(0, 0%, 0%, 0)">HSLAGHOST hsla with zero alpha.</p>\n'
        '    <p style="color: #00000000">HEXGHOST eight-digit hex, alpha 00.</p>\n'
        '    <p style="color: rgba(0,0,0,1)">VISIBLEOPAQUE opaque rgba must survive.</p>\n'
        '    <p style="color: #000000">VISIBLEHEXSIX six-digit hex ends in 00 but has no alpha.</p>',
    ),
    (
        "Text Fill Colour",
        '    <p class="fill">FILLGHOST hidden by -webkit-text-fill-color.</p>\n'
        "    <p>VISIBLEFOUR must survive.</p>",
    ),
    (
        "Visibility Hidden",
        '    <p class="ghost">VISGHOST visibility hidden on the paragraph itself.</p>\n'
        '    <div class="ghost">\n'
        "      <p>VISGHOSTCHILD a child of a hidden container.</p>\n"
        '      <img src="img/page.png" alt=""/>\n'
        "    </div>\n"
        '    <p style="visibility: collapse">COLLAPSEGHOST collapse behaves like hidden.</p>\n'
        "    <p>VISIBLEFIVE must survive.</p>",
    ),
    (
        "Opacity Zero",
        '    <div class="vanish">\n'
        "      <p>OPACITYGHOST a child of an opacity-zero container.</p>\n"
        '      <img src="img/page.png" alt=""/>\n'
        "    </div>\n"
        '    <p style="opacity: 0.0">OPACITYGHOSTTWO zero spelt with a decimal.</p>\n'
        '    <p style="opacity: 0.5">VISIBLEDIM half opacity is dim, not invisible.</p>\n'
        "    <p>VISIBLESIX must survive.</p>",
    ),
    (
        "Explicit Colours Survive",
        '    <p class="red">VISIBLERED red is a colour, not transparency.</p>\n'
        '    <p style="color: transparent; color: black">VISIBLEOVERRIDE the later declaration wins.</p>\n'
        '    <p style="color: Transparent">CASEGHOST the keyword is case-insensitive.</p>\n'
        "    <p>VISIBLESEVEN must survive.</p>",
    ),
    (
        "Nothing Hidden Here",
        "    <p>A control chapter: every word in it must appear in the golden.</p>\n"
        '    <p class="red">Including this red paragraph, which is coloured but visible.</p>',
    ),
]


def main():
    manifest, spine, files = [], [], {}

    files["style.css"] = CSS
    manifest.append('<item id="css" href="style.css" media-type="text/css"/>')
    files["img/page.png"] = png_gray(40, 60)
    manifest.append('<item id="page" href="img/page.png" media-type="image/png"/>')

    for i, (title, body) in enumerate(CHAPTERS, start=1):
        name = f"chapter{i}.xhtml"
        files[name] = CHAPTER.format(title=title, body=body)
        manifest.append(f'<item id="chapter{i}" href="{name}" media-type="application/xhtml+xml"/>')
        spine.append(f'<itemref idref="chapter{i}"/>')

    opf = (
        '<?xml version="1.0" encoding="utf-8"?>\n'
        '<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">\n'
        '  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">\n'
        "    <dc:title>Invisible Text</dc:title>\n"
        "    <dc:creator>Test Corpus</dc:creator>\n"
        "    <dc:language>en</dc:language>\n"
        '    <dc:identifier id="bookid">invisible-text</dc:identifier>\n'
        "  </metadata>\n"
        "  <manifest>\n    " + "\n    ".join(manifest) + "\n  </manifest>\n"
        "  <spine>\n    " + "\n    ".join(spine) + "\n  </spine>\n"
        "</package>\n"
    )

    container = (
        '<?xml version="1.0"?>\n'
        '<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">\n'
        '  <rootfiles><rootfile full-path="OEBPS/content.opf" '
        'media-type="application/oebps-package+xml"/></rootfiles>\n'
        "</container>\n"
    )

    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", container)
        z.writestr("OEBPS/content.opf", opf)
        for name, content in files.items():
            z.writestr("OEBPS/" + name, content)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
