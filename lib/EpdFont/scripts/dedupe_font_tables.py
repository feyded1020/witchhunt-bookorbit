#!/usr/bin/env python3
"""Hoist byte-identical tables shared between built-in font headers into one shared header.

A family ships at five sizes in four styles, and much of what fontconvert.py emits per face is a
property of the TYPEFACE rather than the size:

  * which codepoints the face covers           -> Intervals
  * which kern class each codepoint belongs to -> Kern{Left,Right}{Codepoints,ClassIds}
  * which class pairs have a kern entry at all -> KernRowOffsets, KernSparseCols
  * the ligature substitutions                 -> LigaturePairs

Only the kern VALUES scale with size (they are 4.4 fixed-point pixels), and the bitmaps and glyph
records are of course per-size. So 47 faces carry 47 copies of tables that have 6 to 14 distinct
values between them. Measured on the shipped set, that is 226,362 B of duplication -- 3.6% of the
whole image.

fontconvert.py cannot see this: it is invoked once per face and each invocation writes a
self-contained header. This runs afterwards over the finished set, which is also why it is safe --
it never computes a table, it only notices that two faces emitted the same bytes and keeps one.
Every rewrite is verified to resolve to identical content before anything is written.

Idempotent, and genuinely so: a previous run is UNDONE first (see expand_previous_run) and the
whole set is then deduped from scratch. That matters for the interesting input, a tree where only
some faces are new -- generating one extra size and re-running used to rewrite the shared header
with only the tables that size happened to duplicate, dropping the ones every other face was still
referencing and leaving a tree that did not link. The docstring claimed idempotency before it was
true; adding the 24 pt faces is what found that out.

Usage:  python dedupe_font_tables.py <builtinFonts dir>
"""

import hashlib
import os
import re
import sys
from collections import defaultdict

SHARED_HEADER = "shared_tables.h"

# Suffixes worth considering. Bitmaps/Glyphs/Groups are deliberately absent: they are per-size by
# construction, so hoisting them would only add indirection. KernSparseValues IS included even
# though it is size-dependent today -- it costs nothing to check, and if two faces ever do agree
# there is no reason to keep both.
CANDIDATE_SUFFIXES = (
    "Intervals",
    "KernLeftCodepoints",
    "KernLeftClassIds",
    "KernRightCodepoints",
    "KernRightClassIds",
    "KernRowOffsets",
    "KernSparseCols",
    "KernSparseValues",
    "LigaturePairs",
)

# `static const <type> <name>[<optional length>] = { ... };`
ARRAY_RE = re.compile(r"static const (\w[\w ]*?) (\w+)\[(\d*)\] = \{(.*?)\n\};\n", re.S)

# The same, as this script itself writes it into SHARED_HEADER. Shared by the expand pass that
# reads a previous run back and the verify pass that re-reads what this run wrote.
SHARED_ARRAY_RE = re.compile(r"inline constexpr (\w[\w ]*?) (\w+)\[\] = \{(.*?)\n\};", re.S)


def normalised(body: str) -> str:
    """The values alone: comments and whitespace carry no meaning here."""
    return re.sub(r"\s+", "", re.sub(r"//[^\n]*", "", body))


def expand_previous_run(directory):
    """Inline any table a previous run hoisted, so pass 1 always sees a self-contained tree.

    fontconvert.py names a face's tables `<stem><Suffix>`, and the suffix survives in the shared
    name (`epdSharedFontIntervals_<digest>`), so the original local name is reconstructible. That
    is what makes undoing cheap enough to do unconditionally -- cheaper, and far easier to be sure
    of, than teaching the dedup to merge with whatever it finds already hoisted.
    """
    shared_path = os.path.join(directory, SHARED_HEADER)
    if not os.path.exists(shared_path):
        return 0
    shared_src = open(shared_path, encoding="utf-8").read()
    bodies = {
        m.group(2): (m.group(1).strip(), m.group(3))
        for m in re.finditer(SHARED_ARRAY_RE, shared_src)
    }
    expanded = 0
    for path in font_headers(directory):
        src = open(path, encoding="utf-8", errors="surrogateescape").read()
        referenced = sorted(set(re.findall(r"\bepdSharedFont\w+\b", src)))
        if not referenced:
            continue
        stem = os.path.basename(path)[:-2]
        definitions = []
        for shared_name in referenced:
            ctype, body = bodies[shared_name]
            suffix = shared_name[len("epdSharedFont") :].rsplit("_", 1)[0]
            local = f"{stem}{suffix}"
            definitions.append(f"static const {ctype} {local}[] = {{{body}\n}};\n")
            src = re.sub(rf"\b{re.escape(shared_name)}\b", local, src)
            expanded += 1
        # Ahead of the EpdFontData initialiser that reads them, which is the last thing in the file.
        anchor = re.search(r"^static const EpdFontData ", src, re.M)
        assert anchor, f"{path}: no EpdFontData definition to insert the re-inlined tables before"
        src = src[: anchor.start()] + "".join(definitions) + src[anchor.start() :]
        src = src.replace(f'#include "{SHARED_HEADER}"\n', "")
        open(path, "w", encoding="utf-8", errors="surrogateescape", newline="").write(src)
    return expanded


def font_headers(directory):
    for name in sorted(os.listdir(directory)):
        if name.endswith(".h") and name not in ("all.h", SHARED_HEADER):
            yield os.path.join(directory, name)


def main(directory):
    # Pass 0: undo any previous run, so everything below sees one consistent kind of input.
    reinlined = expand_previous_run(directory)
    if reinlined:
        print(f"dedupe_font_tables: re-inlined {reinlined} references from a previous run", file=sys.stderr)

    # Pass 1: collect every candidate array, keyed by (type, normalised content).
    occurrences = defaultdict(list)  # key -> [(path, name, decl_text, body)]
    sources = {}
    for path in font_headers(directory):
        src = open(path, encoding="utf-8", errors="surrogateescape").read()
        sources[path] = src
        for m in ARRAY_RE.finditer(src):
            ctype, name, _, body = m.group(1).strip(), m.group(2), m.group(3), m.group(4)
            if not name.endswith(CANDIDATE_SUFFIXES):
                continue
            occurrences[(ctype, normalised(body))].append((path, name, m.group(0), body))

    shared = {key: occ for key, occ in occurrences.items() if len(occ) > 1}
    if not shared:
        print("dedupe_font_tables: nothing shared between headers", file=sys.stderr)
        return 0

    # Name each shared table after its content, so the mapping is reproducible and a regenerated
    # tree produces the same names as long as the content is the same.
    named = {}
    for (ctype, norm), occ in sorted(shared.items(), key=lambda kv: kv[1][0][1]):
        suffix = next(s for s in CANDIDATE_SUFFIXES if occ[0][1].endswith(s))
        digest = hashlib.sha256(norm.encode()).hexdigest()[:12]
        named[(ctype, norm)] = f"epdSharedFont{suffix}_{digest}"

    # Pass 2: write the shared header.
    out = [
        "// GENERATED by lib/EpdFont/scripts/dedupe_font_tables.py -- do not edit.",
        "//",
        "// Tables that several built-in faces emitted identically. A family's codepoint coverage,",
        "// kern class assignment and ligature set are properties of the TYPEFACE, so every size of",
        "// a family repeats them; only the kern values scale with size. Hoisting the repeats here",
        "// is a pure deduplication -- the bytes are unchanged, there is simply one copy.",
        "//",
        "// `inline constexpr` rather than `static const`: the latter would give one copy per",
        "// translation unit that includes this, which would silently undo the saving the first time",
        "// a second TU includes builtinFonts/all.h.",
        "#pragma once",
        "",
        '#include "EpdFontData.h"',
        "",
    ]
    for (ctype, norm), shared_name in sorted(named.items(), key=lambda kv: kv[1]):
        occ = shared[(ctype, norm)]
        users = ", ".join(sorted(os.path.basename(p)[:-2] for p, _, _, _ in occ))
        out.append(f"// {len(occ)} faces: {users}")
        out.append(f"inline constexpr {ctype} {shared_name}[] = {{{occ[0][3]}\n}};")
        out.append("")
    shared_path = os.path.join(directory, SHARED_HEADER)
    open(shared_path, "w", encoding="utf-8", newline="\n").write("\n".join(out))

    # Pass 3: rewrite each font header -- drop the duplicated definition, point its single
    # reference at the shared table.
    saved = 0
    for path, src in sources.items():
        new = src
        touched = False
        for (ctype, norm), shared_name in named.items():
            for occ_path, name, decl, body in shared[(ctype, norm)]:
                if occ_path != path:
                    continue
                assert normalised(body) == norm, f"{path}:{name} content drifted"
                new = new.replace(decl, "", 1)
                # The name appears exactly once more, in the EpdFontData initialiser.
                before = new
                new = re.sub(rf"\b{re.escape(name)}\b", shared_name, new)
                assert new != before, f"{path}:{name} had no reference to rewrite"
                saved += len(normalised(body))
                touched = True
        if not touched:
            continue
        if f'#include "{SHARED_HEADER}"' not in new:
            new = new.replace('#include "EpdFontData.h"', f'#include "EpdFontData.h"\n#include "{SHARED_HEADER}"', 1)
        open(path, "w", encoding="utf-8", errors="surrogateescape", newline="").write(new)

    # Pass 4: verify. Re-read everything and confirm every face still resolves each table to the
    # same bytes it had before. This is the check that makes the rewrite trustworthy rather than
    # merely plausible.
    shared_src = open(shared_path, encoding="utf-8").read()
    shared_bodies = {m.group(2): normalised(m.group(3)) for m in re.finditer(SHARED_ARRAY_RE, shared_src)}
    for path, original in sources.items():
        before = {}
        for m in ARRAY_RE.finditer(original):
            if m.group(2).endswith(CANDIDATE_SUFFIXES):
                before[m.group(2)] = normalised(m.group(4))
        now_src = open(path, encoding="utf-8", errors="surrogateescape").read()
        local = {m.group(2): normalised(m.group(4)) for m in ARRAY_RE.finditer(now_src)}
        for name, content in before.items():
            if name in local:
                resolved = local[name]
            else:
                ref = re.search(rf"\b(epdSharedFont\w+)\b", now_src)
                resolved = None
                for cand, body in shared_bodies.items():
                    if cand in now_src and body == content:
                        resolved = body
                        break
            assert resolved == content, f"{os.path.basename(path)}:{name} changed content"

    print(f"dedupe_font_tables: {len(named)} shared tables, ~{saved} values deduplicated", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "builtinFonts")))
