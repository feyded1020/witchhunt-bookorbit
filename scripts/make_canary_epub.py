#!/usr/bin/env python3
"""Build the canary EPUB used to check cache enciphering on a device.

docs/protected-content-plan.md §3 asks whether a protected book's derived cache really is
enciphered at rest. That question is only awkward because ordinary book text is hard to search
for mechanically. This builds a book whose every paragraph carries a fixed marker, so the check
becomes exact: find the marker anywhere in the cache directory and the enciphering failed.

Writes two files (default: test/device/):

    canary.epub          a small, valid EPUB whose text is the marker, repeated
    canary.epub.rights   an empty sidecar - the marker the firmware currently reads as
                         "this book is protected", see Epub::cacheEnciphered()

Copy BOTH onto the card to test the protected path. Copy only the .epub - without the sidecar -
to get the control case, where the cache is expected to be readable.

    python scripts/make_canary_epub.py
"""

import argparse
import pathlib
import zipfile

CANARY = "ZZCANARYZZ"

CONTAINER_XML = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

CONTENT_OPF = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">urn:uuid:canary-0000-0000-0000-000000000001</dc:identifier>
    <dc:title>Cache Canary</dc:title>
    <dc:creator>Protected Content Test</dc:creator>
    <dc:language>en</dc:language>
  </metadata>
  <manifest>
    <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
{items}
  </manifest>
  <spine>
{spine}
  </spine>
</package>
"""

NAV_XHTML = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops">
  <head><title>Contents</title></head>
  <body>
    <nav epub:type="toc"><ol>
{entries}
    </ol></nav>
  </body>
</html>
"""

CHAPTER_XHTML = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml">
  <head><title>Chapter {n}</title></head>
  <body>
    <h1>Chapter {n}</h1>
{paragraphs}
  </body>
</html>
"""


def chapter_body(chapter: int, paragraphs: int, words: int) -> str:
    """Paragraphs of the marker plus a counter, so a partially enciphered file is still
    recognisable and the page layout has something to break lines on."""
    out = []
    for p in range(paragraphs):
        words_text = " ".join(f"{CANARY}{chapter:02d}{p:02d}{w:03d}" for w in range(words))
        out.append(f"    <p>{words_text}</p>")
    return "\n".join(out)


def build(dest: pathlib.Path, chapters: int, paragraphs: int, words: int) -> pathlib.Path:
    dest.mkdir(parents=True, exist_ok=True)
    epub_path = dest / "canary.epub"

    items, spine, toc = [], [], []
    for n in range(1, chapters + 1):
        items.append(f'    <item id="ch{n}" href="ch{n}.xhtml" media-type="application/xhtml+xml"/>')
        spine.append(f'    <itemref idref="ch{n}"/>')
        toc.append(f'      <li><a href="ch{n}.xhtml">Chapter {n}</a></li>')

    with zipfile.ZipFile(epub_path, "w", zipfile.ZIP_DEFLATED) as z:
        # "mimetype" must be first and stored, per the EPUB spec.
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", zipfile.ZIP_STORED)
        z.writestr("META-INF/container.xml", CONTAINER_XML)
        z.writestr("OEBPS/content.opf", CONTENT_OPF.format(items="\n".join(items), spine="\n".join(spine)))
        z.writestr("OEBPS/nav.xhtml", NAV_XHTML.format(entries="\n".join(toc)))
        for n in range(1, chapters + 1):
            z.writestr(
                f"OEBPS/ch{n}.xhtml",
                CHAPTER_XHTML.format(n=n, paragraphs=chapter_body(n, paragraphs, words)),
            )

    rights_path = dest / "canary.epub.rights"
    rights_path.write_bytes(b"")
    return epub_path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dest", type=pathlib.Path, default=pathlib.Path("test/device"))
    parser.add_argument("--chapters", type=int, default=4)
    parser.add_argument("--paragraphs", type=int, default=12, help="paragraphs per chapter")
    parser.add_argument("--words", type=int, default=40, help="marker words per paragraph")
    args = parser.parse_args()

    epub_path = build(args.dest, args.chapters, args.paragraphs, args.words)
    size = epub_path.stat().st_size
    print(f"wrote {epub_path} ({size} bytes) and {epub_path}.rights")
    print(f"marker: {CANARY}")
    print("copy both onto the card for the protected case; the .epub alone for the control case")


if __name__ == "__main__":
    main()
