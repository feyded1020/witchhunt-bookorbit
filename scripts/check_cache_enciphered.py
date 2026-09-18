#!/usr/bin/env python3
"""Check whether a book's cache directory is enciphered at rest.

Point it at a cache directory copied off the card (or at the card itself), after the device has
opened the book and turned a few pages:

    python scripts/check_cache_enciphered.py E:/.crosspoint/epub_12345678
    python scripts/check_cache_enciphered.py E:/.crosspoint/epub_12345678 --expect plain

Two independent checks per file, because either alone can mislead:

  * the canary marker (scripts/make_canary_epub.py) - decisive when present. A marker anywhere
    in the cache means that file holds book text in the clear.
  * a printable-run and entropy estimate - a fallback for real books, where there is no marker.
    Enciphered bytes have high entropy and no long ASCII runs; cache files in the clear carry
    both the format's own tags and the book's words.

Exit status is 0 when the directory matches the expectation, 1 when it does not, so this can
gate a device test rather than just inform one.

Files the format leaves in the clear on purpose are reported as such and ignored: .csalt is a
salt (not secret), and fingerprint.bin holds no book text.
"""

import argparse
import collections
import math
import pathlib
import sys

CANARY = b"ZZCANARYZZ"

# Written deliberately outside the cipher, or carrying no book text.
EXEMPT_NAMES = {".csalt", "fingerprint.bin"}

# Markers the cache formats write in the clear when not enciphered. Finding one is strong
# evidence the file was never through the cipher, independent of the canary.
FORMAT_MARKERS = (b"WBC1", b"<?xml", b"<html", b"<p>", b"<div", b"http://www.idpf.org")


def shannon_entropy(data: bytes) -> float:
    """Bits per byte. Enciphered data sits near 8.0; text and structured records sit well below."""
    if not data:
        return 0.0
    counts = collections.Counter(data)
    total = len(data)
    return -sum((c / total) * math.log2(c / total) for c in counts.values())


def longest_printable_run(data: bytes) -> int:
    best = run = 0
    for byte in data:
        if 32 <= byte <= 126:
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def inspect(path: pathlib.Path) -> dict:
    data = path.read_bytes()
    return {
        "path": path,
        "size": len(data),
        "canary": data.count(CANARY),
        "markers": [m.decode("ascii", "replace") for m in FORMAT_MARKERS if m in data],
        "entropy": shannon_entropy(data),
        "run": longest_printable_run(data),
    }


def verdict(info: dict) -> str:
    """CLEAR, ENCIPHERED or UNCLEAR for one file."""
    if info["size"] == 0:
        return "EMPTY"
    if info["canary"] or info["markers"]:
        return "CLEAR"
    # Thresholds are deliberately generous: a short file can look random by accident, so only
    # call it enciphered when it is both high-entropy AND free of long printable runs.
    if info["entropy"] >= 7.5 and info["run"] <= 24:
        return "ENCIPHERED"
    if info["entropy"] < 6.0 or info["run"] >= 64:
        return "CLEAR"
    return "UNCLEAR"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("cache_dir", type=pathlib.Path, help="a per-book cache directory (epub_*)")
    parser.add_argument(
        "--expect",
        choices=("enciphered", "plain"),
        default="enciphered",
        help="what this directory should look like (default: enciphered)",
    )
    parser.add_argument("--verbose", action="store_true", help="one line per file, not just the failures")
    args = parser.parse_args()

    if not args.cache_dir.is_dir():
        print(f"not a directory: {args.cache_dir}", file=sys.stderr)
        return 2

    files = sorted(p for p in args.cache_dir.rglob("*") if p.is_file())
    if not files:
        print(f"no files under {args.cache_dir} - has the device opened the book yet?", file=sys.stderr)
        return 2

    counts = collections.Counter()
    offenders = []

    print(f"{'verdict':<11} {'entropy':>7} {'run':>5} {'size':>8}  file")
    for path in files:
        info = inspect(path)
        name = path.name
        rel = path.relative_to(args.cache_dir)

        if name in EXEMPT_NAMES:
            counts["exempt"] += 1
            if args.verbose:
                print(f"{'exempt':<11} {info['entropy']:>7.2f} {info['run']:>5} {info['size']:>8}  {rel}")
            continue

        state = verdict(info)
        counts[state.lower()] += 1
        wrong = (args.expect == "enciphered" and state in ("CLEAR", "UNCLEAR")) or (
            args.expect == "plain" and state == "ENCIPHERED"
        )
        if wrong:
            offenders.append((rel, info, state))
        if args.verbose or wrong:
            detail = ""
            if info["canary"]:
                detail = f"  <-- {info['canary']} canary hits"
            elif info["markers"]:
                detail = f"  <-- {', '.join(info['markers'])}"
            print(f"{state:<11} {info['entropy']:>7.2f} {info['run']:>5} {info['size']:>8}  {rel}{detail}")

    print()
    print(f"{len(files)} files: " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))

    if args.expect == "enciphered":
        if offenders:
            print(f"\nFAIL: {len(offenders)} file(s) still readable - these hold book text in the clear")
            return 1
        print("\nPASS: nothing readable in this cache directory")
        return 0

    if counts["enciphered"]:
        print(f"\nFAIL: {counts['enciphered']} file(s) enciphered in a cache that should be plain")
        return 1
    print("\nPASS: control cache is readable, as expected for an unprotected book")
    return 0


if __name__ == "__main__":
    sys.exit(main())
