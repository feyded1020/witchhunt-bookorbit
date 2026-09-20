# Witch Reader + BookOrbit

A build of [Witch(hunt) Reader](https://github.com/jpirnay/witchhunt-reader) with the
[BookOrbit](https://github.com/agosez/CrossInk-Bookorbit) sync features of CrossInk-Bookorbit,
in place of KOReader Sync. Built and size-checked for the **Xteink X4 Pro**.

Everything Witch Hunt does is unchanged except where noted below. BookOrbit code is ported from
CrossInk-Bookorbit (MIT) and adapted to Witch Hunt's network stack, stores and UI.

## What BookOrbit sync does here

| Feature | How it works on the device |
|---|---|
| **Reading progress**: two-way | Reader menu → Sync tab (pull, push, or compare), or automatically when you close a book (**Auto-Push on Book Close**). Smart mode resolves conflicts without asking. |
| **Reading stats** | Every page you read is recorded (time on page + position) and uploaded on the next sync, feeding BookOrbit's dashboard, streaks and pace. Timestamps are corrected against network time at upload. |
| **Highlights**: two-way | Reader menu → *Highlight Text*: move to the first word, **Confirm**, extend to the last word, **Confirm** again. Highlights are underlined on the page and listed under *Highlights* (jump / delete). Web highlights arrive on the next sync; deletions go both ways. |
| **Bookmarks**: two-way | The existing star-page bookmarks sync both ways, deletions included. Bookmarks made on the web land in the right chapter and settle on the exact page the first time you open that chapter. |
| **Catalog** | Home → *BookOrbit* (appears once an account is set), or Settings → BookOrbit Sync → *Browse Catalog*. Same layout as CrossInk-Bookorbit: the server's sections with book counts (continue reading, recently added, libraries, collections, authors, series, all books), then *On device* and *In progress* (these two also work offline), then search. Books already on the SD card are marked • and open directly; others download and open. |

If the apply/upload question is left unanswered, the device stays awake for five minutes rather
than sleeping mid-decision (sleeping there would abandon the sync). After that it is allowed to
sleep, the sync is recorded as unfinished, and the next time you open that book the reader says
**"Not synced - progress stayed on this device"** so nothing fails silently.

Matching is by the book file's content hash (KOReader's "Binary" method), so the same EPUB file
must be on the reader and in BookOrbit. Books downloaded from the catalog always match.

## Setup

1. In BookOrbit: **Settings → Devices & Sync → KOReader**, create sync credentials.
2. On the reader: **Settings → BookOrbit Sync**, enter:
   - **Server URL**: e.g. `https://books.example.com` (a pasted `/api/v1/koreader` suffix is stripped automatically)
   - **Username** / **Password**: the KOReader sync credentials from step 1
3. **Authenticate** tests the login.

The same fields are on the web settings page (File Transfer → web UI → Settings → BookOrbit Sync).

### Coming from CrossInk-Bookorbit

- Your login carries over: the settings file (`/.crosspoint/bookorbit.json`) is the same.
- This firmware registers with BookOrbit as a **new device** (`witchreader-…`, not `crossink-…`)
  and keeps its per-book state in `/.crosspoint/wr_book_<hash>/`. That is deliberate: it does not
  read CrossInk's local highlight/bookmark files, and reusing CrossInk's device id would make the
  server read their absence as deletions. As a new device, the first sync of each book
  **downloads** your existing highlights and bookmarks from BookOrbit instead.
- Leave the old CrossInk device in BookOrbit alone (don't delete it) until you've synced your
  books once on this firmware.

### HTTPS

Requests carry your credentials, so the server certificate is verified against Witch Hunt's
built-in root list (Let's Encrypt, Google Trust Services, USERTrust…), which covers a typical
Cloudflare or Let's Encrypt setup. For a server behind a private CA or a self-signed certificate,
turn on **Settings → System → Skip HTTPS validation (risky)**. That also affects every other
network feature, so only do it if you have to.

## Flashing

1. Build it (below) or take `firmware.bin` from a build of this branch.
2. Open the [CrossPoint flash tools](https://crosspointreader.com/#flash-tools) in Chrome/Edge,
   choose **Custom .bin**, upload `firmware.bin`, connect the X4 Pro by USB and flash.
   (Already on Witch Hunt? Copy `firmware.bin` to the SD card and use **SD Firmware Update**.)

**Don't use the built-in online update check** on this build: it installs official Witch Hunt
releases, which would replace this build (your books and settings stay on the SD card).

Before your first flash, note which firmware you are on now and keep its `.bin`, so you can flash
back to it the same way.

## Building

```sh
python3 -m venv .venv && .venv/bin/pip install platformio
.venv/bin/pio run -e x4pro_gh_release      # release build -> .pio/build/x4pro_gh_release/firmware.bin
.venv/bin/pio run -e x4pro -t upload       # dev build straight to a USB-connected X4 Pro
```

The app slot is 6,553,600 bytes; see the commit log for the size of each build.

## What was removed or changed from Witch Hunt

- **KOReader Sync is gone** (client, credential store, settings). BookOrbit's sync replaces it,
  keeping Witch Hunt's sync flow (auto pull/push on open/close, smart conflict mode, min pages).
  Removing it is also what keeps the build inside the flash partition.
- **Bookmark file** is version 3 (adds creation time and a layout-independent position); older
  files still load. Older Witch Hunt builds will not read a v3 file.
- **HalClock** gains an "NTP synced" hook that BookOrbit's `WallClock` uses to correct queued
  reading-session timestamps.

## Limitations

- A highlight has to fit on one page (CrossInk-Bookorbit can select across page turns).
- Highlights show as underlines; BookOrbit's highlight colours/styles and notes are not shown.
- Only EPUB is synced (not TXT/XTC/Markdown).
- Only English UI text was added for the new screens; other languages fall back to English there.

## Where the code is

| Area | Files |
|---|---|
| Sync client, stores, stats queue | `lib/BookOrbitSync/`, `lib/WallClock/` |
| Highlight xpointer resolver | `lib/EpubPosition/` (moved from expat onto Witch Hunt's `SaxParser`) |
| Stats/highlight/bookmark exchange | `src/bookorbit/BookOrbitSyncExtras.cpp` |
| Highlights | `src/bookorbit/HighlightStore.*`, `HighlightRenderer.*`, `src/activities/reader/HighlightsActivity.*`, highlight mode in `DictionaryWordSelectActivity` |
| Catalog | `src/bookorbit/BookOrbitCatalogClient.*`, `src/activities/browser/BookOrbitCatalogActivity.*` |
| Sync / settings / login screens | `src/activities/reader/BookOrbitSyncActivity.*`, `src/activities/settings/BookOrbit*Activity.*` |
| Tests | `test/bookmark_sync/` |
