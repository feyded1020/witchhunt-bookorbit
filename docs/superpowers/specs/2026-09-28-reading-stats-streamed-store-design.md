# Reading stats without a resident history — design

> **Superseded in part.** The file layer — sections 3, 4 and 6 and the "no format change"
> non-goal — is replaced by `2026-09-28-reading-stats-slot-file-design.md`, after the device
> measured 2.7 s for a worst-case session end. The rest stands and is built.
>
> **Status.** Agreed in conversation on 2026-09-28, section by section; this document is the
> written form for review before an implementation plan. Builds on `feat/stats-remove-book`
> (per-book removal plus the streamed web dashboard), which introduced `ReadingStatsFile` and
> the static arithmetic helpers this design leans on. Companion to
> `docs/memory-audit-2026-09.md`, findings **F8** and **R8**.

## Problem

Every on-device consumer of the reading history loads **all of it** into RAM: the whole
`reading-stats.json` is parsed into an ArduinoJson document and then copied into per-book
vectors (`JsonSettingsIO::loadReadingStats`), and every save serialises the whole store the
same way. Measured costs:

- **Load peak ~1.5 KB per book** (F8: 18 books cost a ~27 KB transient).
- **Resident ~420 B per book** while a screen holds it (36 books measured at ~15 KB standing,
  the largest free block dropping from 65 524 to 26 612; commit 0b3f288e0).
- **The tightest consumer is the session end.** It loads, merges one book and saves on reader
  exit, which R8 moved after the reader's teardown, where 43 556 free / 23 540 contiguous was
  measured (run 17).

So the load stops fitting at the session end at roughly **25–30 books**, not at the
100-book cap R8 set. When it does, `loadFromFile()` reports `NoMemory`, the store stays
unloaded, `saveToFile()` correctly refuses to overwrite the file — and the session that just
ended is **silently lost**. R8 stopped the data loss that used to follow (an empty store saved
over the history); it did not make the session save.

Nothing on the device needs the whole history at once:

| Consumer | Needs | Books |
|---|---|---|
| Session end / mark finished (`ReadingSessionTracker.cpp:56,84`) | merge one book; update totals, day buckets, streak record | 1 |
| Book Info (`BookInfoActivity.cpp:158`), book stats screen | one book (the stats screen also its ETA, which may fall back to the global pace) | 1 |
| Home badge + history line (`BookProgressPresentation.cpp:44,234,273`, called from the themes' `render()`) | the recent books (≤ 10) and the global pace | ≤ 10 |
| Stats summary (`ReadingStatsActivity.cpp`) | totals, streaks, 30-day chart, counts, top 3 by time | all, a few numbers each |
| All-books list (`ReadingStatsBookListActivity.cpp`) | order by time; title, author, time, finished for one screenful | all, one page |
| Web (`/api/stats`, remove, export) | — already streamed on `feat/stats-remove-book` | — |

## Goals

- On-device memory for reading stats is **independent of the number of books**, up to the
  100-book cap: a bounded transient per call and nothing resident beyond a small cache.
- **No session is lost to memory** at any history size up to the cap.
- **Every screen shows what it shows today**, from the same arithmetic.
- **The file format does not change** — no migration, older firmware reads the file, the web
  export stays a verbatim copy.

## Non-goals

Recorded so they are not re-proposed:

- **No format change** — not a summary file plus per-book files, not a binary record file.
  Both were weighed; either needs a migration path, and a stable format was preferred over
  cheaper single-book writes.
- **No change to `StreamingJsonParser`.** It passes `\uXXXX` through undecoded and drops
  strings over 511 bytes. Changing that would change `ReleaseJsonParser` (OTA asset URLs),
  and nothing here needs it: books are copied as bytes, titles are decoded only for display.
- **No detection of books no longer on the card** in this design (see *Books no longer on
  the card*; a follow-up is sketched there).
- **No change to the web side** beyond sharing the new code paths.

## Design

### 1. Architecture

`ReadingStatsStore` stops holding the history. The per-book vector, the global day vector,
`ScopedLoad`, `ensureLoaded()`, `release()` and the loaded/not-loaded state all go. What
remains is a stateless interface over the file plus one small cache:

- **Queries** stream the file once and return only what was asked for. Callers keep what
  they render; a screen scans on entry and draws from its own small copy.
- **Updates** (record session, mark finished, remove) are streamed rewrites: scan, merge one
  book in memory, write a temporary file, read it back, swap it in — the pattern already used
  by `removeBookFromFile()`.

Division of labour:

- **`ReadingStatsFile`** — the file layout: streamed scans, the time-ordered index, reading a
  single book at a byte offset, the edited copy, the JSON writer for one book record. Never
  holds more than one decoded book.
- **`ReadingStatsStore`** — the path, the atomic write, the rules (caps, eviction, merge
  arithmetic — already static helpers), the query API and the recent-books cache.
- **Consumers** — ask for the one thing they show. `BookProgressPresentation` stays the only
  place the Home themes get history figures from.

`JsonSettingsIO::loadReadingStats` / `saveReadingStats` are deleted, as is
`ReadingStatsStore::replaceLoaded()`.

### 2. Reads

**Recent-books cache** (resident, ~0.7 KB).

- Up to 12 entries keyed by docId, each holding what Home draws: total seconds, count of
  dated reading days, last-read epoch, progress, and the book's own pace; plus one shared
  value, the global pace, for books too new to have their own.
- `prefetchRecent(docIds)` — Home calls it on entry with the recent books' docIds. A no-op
  when all are cached; otherwise **one scan** fills the missing entries and the global pace.
- The themes read the cache through `BookProgressPresentation` (`historyLine`,
  `historyLineCompact`, `formatStatus`, `drawBadge`). **No SD access from `render()`**; today
  those calls read the loaded store from the render task.
- **Write-through.** Every update leaves the cache correct without a scan of its own:
  - record session / mark finished — the target's new figures go into its entry (inserted if
    absent, replacing the least recently used entry when full);
  - remove (device or web) and eviction at the cap — the entry is dropped;
  - every rewrite ends with a read-back scan of the new file; that scan's pace sums refresh
    the cached global pace.
- **No staleness path**: the only way to change the file behind the device's back is USB
  drive mode, which always leaves by rebooting (`docs/usb-mass-storage.md`), clearing the
  cache.

**Stats summary screen** — one scan returns the `Summary` (totals, the ≤ 400 day buckets for
streaks and the 30-day chart, book and finished counts, global pace) together with the
time-ordered index; the top 3 rows are read at their offsets. ~3.5 KB while the screen is up.

**All-books list** — one scan builds the index: `{totalSeconds, byte offset}` per book, sorted
descending (800 B at 100 books). Only the visible page is decoded, each row by seeking to its
offset and parsing that one entry (title, author, time, finished). A page turn decodes the new
page (~8 short reads). The index is rebuilt when a child screen reports a removal.

**Book stats screen, Book Info** — one scan decodes the book, days included, plus the global
pace when the book has no pace of its own.

**Titles** are decoded only for display. With the parser unchanged, a title over 511 bytes
shows as its docId and a `\u` escape shows literally (the writer emits those only for control
characters). Neither can reach the file: rewrites copy untouched books as bytes, and the book
being updated takes its title fresh from the reader.

### 3. Writes

One rewrite path, with the edit to the one book as the only difference.

1. **Scan** the file once, recording the header, the global day buckets, the target book's
   full record and byte range and — when a *new* book would exceed 100 — the eviction victim
   (least recently read, ties to less time; as today) with its byte range.
2. **Merge** in memory with the store's existing arithmetic: counters, the target's and the
   global day buckets (60 / 400 caps), folding the run into the streak record. The session
   and finish merges become static functions on plain data, as the removal already is.
3. **Write a temporary file**: new header and global days, every untouched book copied byte
   for byte, the target re-encoded in its place (or appended when new), the victim dropped.
   `writeWithoutTarget()` generalises into a copy with a list of edits (replace range, drop
   range with one separator, append), through a `BufferedPrint` for few SD calls.
4. **Read back and swap.** The temporary file must parse, hold the expected number of books
   and the target with its new total (or not at all, after a removal). Only then does it
   replace the original. The read-back's summary refreshes the cache's global pace.

Per operation:

- **Record session** — creates an entry only for a session with seconds (today's rule); a
  zero-second session still updates an existing book's progress. Title and author come from
  the reader, JSON-escaped by the new writer; the old decoded values are kept only when the
  reader passes an empty one.
- **Mark finished** — as today, creating the entry when needed. **Behaviour fix:** today it
  bypasses the 100-book cap (`markFinished()` never evicts); the shared path applies it.
- **Remove** — the device screen and the web endpoint share one path. The screen confirms,
  removes and closes; the list rebuilds its index when it regains the screen.

### 4. Failure handling

The original file is never touched until a verified replacement exists.

- **Absent file** — a new file is written holding just this book.
- **Malformed file** — set aside as `reading-stats.corrupt.json` and a fresh history started,
  as the loader does today.
- **Out of memory** (the ~6–8 KB of a call) — this one update is dropped and logged; the file
  stays as it was. Same outcome as today's `NoMemory`, but today it takes only ~30 books (a
  ~45 KB load) to reach it.
- **Write or read-back fails** — the temporary file is deleted; the original stays.

`ReadingStatsFile`'s scan reports **Ok / NoMemory / Malformed** instead of a bool, so the
store can tell the transient case from the permanent one (today's loader draws the same line).

### 5. Books no longer on the card

**Rule: deleting a book from the card never touches its reading history.** Only *Remove from
stats* removes a book from the history. A book that was read and then deleted was still read:
its entry, its figures and its share of the totals and streaks all stay. Neither delete path
(the device file browser, the web file manager) touches the stats file today, and this design
adds no hook there.

Entries are keyed by `KOReaderDocumentId::calculateFromFilename()`, the MD5 of the file's
**basename**; no path is stored and nothing checks an entry against the card. So:

- a deleted book's entry stays and is listed like any other; *Remove from stats* removes it,
  since removal works on the docId alone and never opens the book (true already on
  `feat/stats-remove-book`);
- moving a book between folders keeps its history (same basename) — including the move to
  `/COMPLETED` after finishing; renaming it starts a new entry and leaves the old one as
  history;
- such entries hold a slot under the book cap like any other and leave only by the cap's
  least-recently-read eviction or by *Remove from stats*;
- nothing tells the user which entries' books are gone, and nothing can cheaply: without a
  path, finding out means walking the card and hashing every filename.

**Follow-up, not in this design:** an additive `path` field (last path the book was read
from), written at each session end, so the list can mark "not on card" (one `exists()` per
visible row). Older entries lack it until read again, older firmware ignores it, and the
streamed copy carries it through — no migration. Display only: it would not change eviction.

### 6. Costs

- **Memory.** Resident: the cache, ~0.7 KB. Per call: ~6–8 KB peak — parser 0.6 KB, 1 KB
  read block, 1 KB write buffer, up to 2.4 KB of global days plus a working copy, one decoded
  book (≤ ~1 KB), the index (≤ 0.8 KB) — whether the file holds 10 books or 100.
- **Time per pass.** Realistic files (20–50 KB) take tens of ms; the worst case (100 books ×
  60 days, ~110 KB) ~150–250 ms at ~1.5 ms per 1 KB SD read plus parsing. Session end is three
  passes (scan, rewrite, read-back), up to ~0.5–0.8 s in the worst case; run 17 measured
  290 ms for today's load-and-save at 18 books. If the device shows it too slow, the
  read-back is the pass to drop at the session end — the swap stays atomic without it, and
  the rewrite pass, which sees every book anyway, collects the pace sums for the cache instead.
- **Home:** no scan on a warm entry, one after boot.
- **SD wear:** unchanged — every session end rewrites the whole file today as well.
- **Flash:** the JSON load/save, `replaceLoaded()` and the `ScopedLoad` plumbing go; the merge,
  edit copy and record writer come. Expected roughly neutral to +3 KB; measured against the
  base branch.

### 7. Testing

- **Host, pure streaming functions** (`test/reading_stats`, fixtures as hand-derived text):
  every query (summary, index, book at offset, find); every rewrite — existing book, new book,
  new book at the cap with eviction, zero-second session, day merge and trims, streak record,
  mark finished including the cap fix, removal; malformed, absent, out of memory.
- **Host, full cycle.** The store against the file-backed `HalStorage` shim from
  `test/zip_entry_reader` in a temporary directory: write, read-back, rename, cache
  write-through. The in-memory `removeBook()` tests move onto this.
- **Device.** A script generates a synthetic 100-book history (60 days each). Measure free and
  contiguous heap and wall time at reader exit, Home entry, the summary and list screens and
  the web removal; confirm a session recorded at 100 books survives, where today's code drops
  it from ~30.

### 8. Rollout

- Branch `feat/stats-streamed-store` on top of `feat/stats-remove-book`, which ships first as
  its own PR (five commits, device- and browser-tested).
- Suggested order of work, each step leaving the firmware working: (1) record writer, index,
  book-at-offset, tri-state scan in `ReadingStatsFile`; (2) the edited copy and the streamed
  session/finish/remove writes; (3) switch the tracker to them; (4) the recent-books cache and
  Home; (5) the stats screens and Book Info; (6) delete the resident store, `ScopedLoad` and the
  JSON load/save; (7) device measurement.

## Open questions

None blocking.

- **The book cap.** After this design it no longer limits memory, only file size and so the
  time per pass: ~0.45 KB per book typical, ~1.1 KB at the 60-bucket worst case, at roughly
  150–250 ms per 100 KB per pass and three passes at the session end. Books deleted from the
  card keep their slot (section 5), so a larger cap is the lever against slot pressure. It
  stays at 100 through this work; the device measurement (step 7) gives the real time per
  pass, and the cap is then set from a time budget (e.g. reader exit under 0.5 s). Halving the
  per-book day buckets to 30, with a `readingDays` counter so "over N days" stays exact, is
  the option if a larger cap needs the room.
- **The `path` field** (section 5) — a follow-up to decide separately.
