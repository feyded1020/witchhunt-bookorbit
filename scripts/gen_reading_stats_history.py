#!/usr/bin/env python3
"""Writes a synthetic reading-stats.json in the device's format, for measuring the streamed store.

    python scripts/gen_reading_stats_history.py --books 100 --days 60 --out reading-stats.json

Copy the result to the card as /.crosspoint/reading-stats.json (back up the real one first).
"""
import argparse
import json

BASE_DAY = 20000          # a day index well inside the uint16 range
BASE_EPOCH = 1728000000   # any plausible epoch; only ordering matters


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--books", type=int, default=100)
    ap.add_argument("--days", type=int, default=60)
    ap.add_argument("--out", default="reading-stats.json")
    args = ap.parse_args()

    books, global_days = [], {}
    for i in range(args.books):
        days = []
        for d in range(args.days):
            day = BASE_DAY + i * 3 + d
            days.append([day, 600])
            global_days[day] = global_days.get(day, 0) + 600
        books.append({
            "docId": f"{i:032x}", "title": f"Synthetic book number {i} with a title of typical length",
            "author": "Test Author", "totalSeconds": 600 * args.days, "pagesTurned": 20 * args.days,
            "sessions": args.days, "firstReadEpoch": BASE_EPOCH + i * 86400,
            "lastReadEpoch": BASE_EPOCH + (i + args.days) * 86400, "progress": (i * 7) % 100,
            "finishedCount": 0, "lastFinishedEpoch": 0, "finished": False, "days": days,
        })
    doc = {
        "totalSeconds": sum(b["totalSeconds"] for b in books),
        "totalSessions": sum(b["sessions"] for b in books),
        "totalPagesTurned": sum(b["pagesTurned"] for b in books),
        "longestStreak": 1,
        "globalDays": sorted([d, s] for d, s in global_days.items())[-400:],
        "books": books,
    }
    with open(args.out, "w", encoding="utf-8") as f:
        json.dump(doc, f, separators=(",", ":"), ensure_ascii=False)
    print(f"wrote {args.out}: {args.books} books x {args.days} days")


if __name__ == "__main__":
    main()
