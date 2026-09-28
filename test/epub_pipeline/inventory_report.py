#!/usr/bin/env python3
"""Symbolize and tabulate epub_build_inventory output.

Usage: inventory_report.py <epub_build_inventory binary> <spine_N.txt> [...] > report.md

For each spine file: the build's heap and arena summary, its phases, every heap site the build
touched (live bytes at build start, at the build's heap peak, at build end; allocations made), and
the arena's contents at the arena's own peak and at the heap's peak, grouped by the code that made
them. A site is named by its innermost frame in the repository's own sources (lib/, src/), with
the next such frame up the inline chain / call stack as context.
"""
import collections
import os
import re
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SKIP_FILES = ("BuildArena.h", "HeapTrack.cpp", "InventoryMain.cpp", "Memory.h")


def symbolize(binary, addrs):
    """addr -> list of (function, file:line), innermost first."""
    addrs = sorted({a for a in addrs if a and a != "0x0"})
    out = {}
    if not addrs:
        return out
    proc = subprocess.run(
        ["addr2line", "-e", binary, "-f", "-C", "-i", "-a"] + addrs, capture_output=True, text=True, check=False
    )
    cur = None
    lines = proc.stdout.splitlines()
    i = 0
    while i < len(lines):
        line = lines[i]
        if re.fullmatch(r"0x[0-9a-f]+", line):
            cur = "0x" + line[2:].lstrip("0") if line != "0x0" else "0x0"
            cur = hex(int(line, 16))
            out[cur] = []
            i += 1
            continue
        func = line
        loc = lines[i + 1] if i + 1 < len(lines) else "??:0"
        out.setdefault(cur, []).append((func, loc))
        i += 2
    return out


def app_frames(chain):
    """Frames from our own sources, innermost first."""
    res = []
    for func, loc in chain:
        path = loc.split(":")[0]
        if not path.startswith(REPO):
            continue
        rel = os.path.relpath(path, REPO)
        if not (rel.startswith("lib/") or rel.startswith("src/")):
            continue
        if any(rel.endswith(s) for s in SKIP_FILES):
            continue
        line = loc.split(":")[1].split(" ")[0] if ":" in loc else "?"
        short_func = re.sub(r"\(.*", "", func)
        res.append(f"{short_func} ({os.path.basename(rel)}:{line})")
    return res


def site_name(sym, pcs):
    frames = []
    for pc in pcs:
        frames.extend(app_frames(sym.get(hex(int(pc, 16)), [])) if pc and pc != "0x0" else [])
    if not frames:
        return "?", ""
    return frames[0], (frames[1] if len(frames) > 1 else "")


def classify(count, start, peak, end):
    if end > start:
        return "outlives build"
    if peak - start <= 0:
        return "transient"
    if count <= 16:
        return "held (few)"
    return "held (many)"


def parse(path):
    d = {"summary": {}, "phases": [], "heap": [], "arena_ap": [], "arena_hp": [], "large": []}
    kv = lambda s: dict(x.split("=", 1) for x in s.split() if "=" in x)
    with open(path) as f:
        for line in f:
            tag, _, rest = line.strip().partition(" ")
            if tag == "SUMMARY":
                d["summary"] = kv(rest)
            elif tag == "PHASE":
                name, _, r = rest.partition(" ")
                p = kv(r)
                p["name"] = name
                d["phases"].append(p)
            elif tag == "HEAP":
                d["heap"].append(kv(rest))
            elif tag == "ARENA_AT_ARENA_PEAK":
                d["arena_ap"].append(kv(rest))
            elif tag == "ARENA_AT_HEAP_PEAK":
                d["arena_hp"].append(kv(rest))
            elif tag == "ARENA_LARGE":
                d["large"].append(kv(rest))
    return d


def arena_table(sym, entries, title):
    groups = collections.OrderedDict()
    for e in entries:
        name, ctx = site_name(sym, [e["f0"], e["f1"], e["f2"], e["f3"]])
        key = (name, ctx)
        g = groups.setdefault(key, {"bytes": 0, "count": 0, "first": int(e["off"])})
        g["bytes"] += int(e["bytes"])
        g["count"] += 1
    total = sum(g["bytes"] for g in groups.values())
    print(f"\n#### {title}: {total} B in {len(entries)} allocations\n")
    print("| bytes | allocs | first offset | site | called from |")
    print("|---:|---:|---:|---|---|")
    for (name, ctx), g in sorted(groups.items(), key=lambda kv_: -kv_[1]["bytes"]):
        print(f"| {g['bytes']} | {g['count']} | {g['first']} | {name} | {ctx} |")


def main():
    binary = sys.argv[1]
    for path in sys.argv[2:]:
        d = parse(path)
        addrs = []
        for h in d["heap"]:
            addrs += [h["pc"], h["ctx1"], h["ctx2"]]
        for e in d["arena_ap"] + d["arena_hp"]:
            addrs += [e["f0"], e["f1"], e["f2"], e["f3"]]
        addrs += [e["f0"] for e in d["large"]]
        sym = symbolize(binary, [hex(int(a, 16)) for a in addrs if a])
        s = d["summary"]
        base, peak, end = int(s["heapBase"]), int(s["heapPeak"]), int(s["heapEnd"])
        print(f"\n## {os.path.basename(path)}\n")
        print(
            f"Heap: base {base}, peak {peak} (**+{peak - base}**), end {end} (+{end - base}). "
            f"Arena: capacity {s['arenaCap']}, peak {s['arenaHighWater']}; "
            f"arena in use at the heap's peak {s['arenaAtHeapPeak']}; "
            f"heap above base at the arena's peak +{int(s['heapAtArenaPeak']) - base}."
        )
        print("\n| phase | heap at start (+base) | heap peak (+base) | arena at start | arena peak |")
        print("|---|---:|---:|---:|---:|")
        for p in d["phases"]:
            print(
                f"| {p['name']} | +{int(p['heapStart']) - base} | +{int(p['heapPeak']) - base} | "
                f"{p['arenaStart']} | {p['arenaPeak']} |"
            )
        # Heap sites, merged by resolved site.
        merged = collections.OrderedDict()
        for h in d["heap"]:
            name, ctx = site_name(sym, [h["pc"], h["ctx1"], h["ctx2"]])
            key = (name, ctx)
            m = merged.setdefault(key, {"count": 0, "bytes": 0, "start": 0, "peak": 0, "end": 0, "max": 0})
            for k in ("count", "bytes", "start", "peak", "end"):
                m[k] += int(h[k])
            m["max"] = max(m["max"], int(h["max"]))
        rows = []
        for (name, ctx), m in merged.items():
            dpeak, dend = m["peak"] - m["start"], m["end"] - m["start"]
            if m["count"] == 0 and dpeak == 0 and dend == 0:
                continue
            rows.append((dpeak, dend, m, name, ctx))
        rows.sort(key=lambda r: (-r[0], -r[2]["bytes"]))
        held = sum(r[0] for r in rows if r[0] > 0)
        print(f"\n#### Heap held above base at the build's peak: {held} B (sum of positive site deltas)\n")
        print("| at peak | at end | allocs | cumulative | largest | class | site | called from |")
        print("|---:|---:|---:|---:|---:|---|---|---|")
        for dpeak, dend, m, name, ctx in rows:
            if dpeak <= 0 and dend == 0 and m["max"] < 1024:
                continue  # small churn that holds nothing at the peak: summarised below
            cls = classify(m["count"], m["start"], m["peak"], m["end"])
            print(f"| {dpeak} | {dend} | {m['count']} | {m['bytes']} | {m['max']} | {cls} | {name} | {ctx} |")
        churn = [r for r in rows if r[0] <= 0 and r[1] == 0 and r[2]["max"] < 1024]
        print(
            f"\n{len(churn)} further sites allocated {sum(r[2]['bytes'] for r in churn)} B in "
            f"{sum(r[2]['count'] for r in churn)} allocations (all < 1 KB each) and held nothing at the peak."
        )
        print("\n#### Arena allocations of 4 KB or more over the build\n")
        print("| bytes | times | site |")
        print("|---:|---:|---|")
        for e in sorted(d["large"], key=lambda x: (-int(x["bytes"]), -int(x["count"]))):
            name, _ = site_name(sym, [e["f0"]])
            print(f"| {e['bytes']} | {e['count']} | {name} |")
        arena_table(sym, d["arena_ap"], "Arena contents at the arena's peak")
        arena_table(sym, d["arena_hp"], "Arena contents at the heap's peak")


if __name__ == "__main__":
    main()
