# Device test: cache enciphering for protected books

Checks the Phase 0 mechanism from [docs/protected-content-plan.md](../../docs/protected-content-plan.md)
on real hardware: a protected book's derived cache must be unreadable on the card, the book must
still read normally, and an ordinary book must be unaffected.

## What is here

| File | Purpose |
|---|---|
| `canary.epub` | a small book whose every word is the marker `ZZCANARYZZ` + a counter |
| `canary.epub.rights` | empty sidecar; the firmware currently reads its presence as "this book is protected" |
| `../../scripts/make_canary_epub.py` | regenerates both (`--chapters`, `--paragraphs`, `--words`) |
| `../../scripts/check_cache_enciphered.py` | verdict per cache file: CLEAR / ENCIPHERED / UNCLEAR |

The marker is the point: "is the cache enciphered?" becomes a grep rather than a judgement.
The checker also works without it — entropy and printable-run heuristics — so it can be pointed
at a real book's cache too.

## What the host tests already cover

Before reaching for hardware: `ctest --test-dir build/test` runs 44 cases
(`EpubPipelineTest.EncipheredCacheIsTransparentAndUnreadable`) that put every corpus book
through the whole pipeline twice — once normally, once with its cache directory under a cipher
scope — and require the page dumps to be identical while the bytes on disk are not, and while
the headers decipher back under the derived key and nonce. That covers the cache-hit path,
where pages are seeked to rather than written in order, which is the part most likely to break.

What it cannot cover, and what the device tests below are for: NVS persistence, the real SD
card, wolfSSL's ChaCha20 (the host uses the portable core), and whether a real book renders.

## 1. The protected path

1. Copy **both** `canary.epub` and `canary.epub.rights` to the card.
2. Open the book on the device and turn several pages, into chapter 2 or 3.
3. Watch serial. Expected, in this order:
   - `[CKEY] Generated a new cache device key` — **first run on this device only**. Seeing it on
     every boot means NVS is not persisting, which is a bug, not a quirk.
   - `[SD] Cache cipher scope: /.crosspoint/epub_<hash>/`
   - `[EBP] Cache enciphered at rest for /canary.epub`
4. **The text on screen must be the canary words, laid out normally.** This is the round-trip
   check: enciphered on write, deciphered on read, through the same page LUT and header patches
   as any other book. Garbled text, a failed open or a rebuild loop means the cipher is wrong
   somewhere — and that is more interesting than the file check below, because it is the
   failure mode a user would hit.
5. Power off, put the card in a PC, and run:

   ```
   python scripts/check_cache_enciphered.py <card>/.crosspoint/epub_<hash>
   ```

   Expect `PASS: nothing readable in this cache directory`. Exit status is 0/1, so this can gate
   a script. `.csalt` and `fingerprint.bin` are reported as exempt: a salt is not a secret and
   the fingerprint holds no book text.

Use the card, not the web file manager — `/.crosspoint` is dot-prefixed, and `/download`
refuses paths whose last component starts with a dot.

## 2. The control

1. Copy `canary.epub` to the card **without** the `.rights` sidecar (rename it, e.g.
   `canary-plain.epub`, so it gets its own cache directory).
2. Open it, turn a few pages.
3. `python scripts/check_cache_enciphered.py <card>/.crosspoint/epub_<hash> --expect plain`

   Expect `PASS: control cache is readable`. This is the half that proves the first test proved
   something: if both directories came back "enciphered", the checker would be measuring its own
   thresholds rather than the firmware.

## 3. Persistence across a reboot

Reboot the device and reopen the protected book. It must render normally **without** rebuilding
the cache, which is what shows the device key survived in NVS and the salt was read back rather
than regenerated. A rebuild here means the key or the salt was lost — the book would still be
readable, so only the serial log and the timing give it away.

## 4. No regression for an ordinary book

The performance contract (plan §2, RULE P1) is that an unprotected book pays nothing. Two checks:

- On device: open any ordinary book and run the render benchmark (`ENABLE_BENCHMARKS` build),
  then compare forward/backward ms per turn against the same book on `master`.
- On host: `ctest --test-dir build/test` must stay green — the cache *format* is unchanged, so
  every existing golden still applies byte for byte.

## Known behaviour worth observing, not asserting

- **Deleting `.csalt`** orphans that cache directory: its files decipher to noise, fail their
  header checks and get rebuilt. Fine, but slow — worth watching once so the failure mode is
  familiar.
- **Moving a card between devices** has the same effect, by design: the device key never leaves
  NVS, so another reader cannot read the first one's caches.
- A cache written *before* this firmware is plaintext; adding the sidecar later does not
  retro-encipher it. Delete the book's cache directory when switching a book to protected.
