#!/usr/bin/env python3
"""Prove a deduplicated builtinFonts tree is byte-equivalent to a pre-dedup copy of it.

Written deliberately as a SEPARATE program from dedupe_font_tables.py, and reading the headers
back from disk rather than reusing anything the transformer computed. A checker that shares the
transformer's parsing would agree with it about a misparse; this one only agrees if the tables a
face actually resolves to are the same bytes they were before.

Usage:  python verify_font_dedup.py <deduped dir> <reference dir>
"""

import os
import re
import sys

ARRAY_RE = re.compile(r"(?:static const|inline constexpr) (\w[\w ]*?) (\w+)\[(?:\d*)\] = \{(.*?)\n\};", re.S)
FONTDATA_RE = re.compile(r"static const EpdFontData (\w+) = \{(.*?)\n\};", re.S)


def values(body: str) -> str:
    return re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", body))


def tables_in(path):
    src = open(path, encoding="utf-8", errors="surrogateescape").read()
    return src, {m.group(2): values(m.group(3)) for m in ARRAY_RE.finditer(src)}


def font_headers(d):
    return sorted(n for n in os.listdir(d) if n.endswith(".h") and n not in ("all.h", "shared_tables.h"))


def resolve(name, local, shared):
    if name in local:
        return local[name]
    if name in shared:
        return shared[name]
    return None  # nullptr / a scalar / an unknown symbol


def initialiser_fields(src, font_name):
    m = FONTDATA_RE.search(src)
    assert m and m.group(1) == font_name, f"no EpdFontData for {font_name}"
    return [ln.strip().rstrip(",").split("//")[0].strip() for ln in m.group(2).splitlines() if ln.strip()]


def shared_tables(directory):
    path = os.path.join(directory, "shared_tables.h")
    return tables_in(path)[1] if os.path.exists(path) else {}


def main(deduped, reference):
    # Resolve shared tables in BOTH trees, not just the deduped one: the reference may itself
    # already be deduped (comparing a tree against itself is how idempotence gets checked).
    shared = shared_tables(deduped)
    ref_shared = shared_tables(reference)

    ref_names = font_headers(reference)
    assert ref_names == font_headers(deduped), "the two trees hold different faces"

    checked = 0
    for name in ref_names:
        font = name[:-2]
        ref_src, ref_local = tables_in(os.path.join(reference, name))
        new_src, new_local = tables_in(os.path.join(deduped, name))

        ref_fields = initialiser_fields(ref_src, font)
        new_fields = initialiser_fields(new_src, font)
        assert len(ref_fields) == len(new_fields), f"{font}: initialiser changed length"

        for i, (before, after) in enumerate(zip(ref_fields, new_fields)):
            b = resolve(before, ref_local, ref_shared)
            a = resolve(after, new_local, shared)
            if b is None and a is None:
                # A scalar or nullptr: it must be unchanged verbatim.
                assert before == after, f"{font}: field {i} changed from {before!r} to {after!r}"
                continue
            assert b is not None and a is not None, f"{font}: field {i} changed kind ({before} -> {after})"
            assert a == b, f"{font}: field {i} ({before} -> {after}) resolves to different bytes"
            checked += 1

        # Nothing a face still defines locally may have silently changed either.
        for tbl, content in new_local.items():
            if tbl in ref_local:
                assert ref_local[tbl] == content, f"{font}: local table {tbl} changed"

    print(f"verify_font_dedup: {len(ref_names)} faces, {checked} table references resolve identically")
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    sys.exit(main(sys.argv[1], sys.argv[2]))
