# Handover: protected content (LCP / Adobe), 2026-09-18

Written at a pause, for whoever picks this up — possibly me, weeks later. The plan of record is
[protected-content-plan.md](protected-content-plan.md); this says where the work actually
stands, what is proven, what only compiles, and which decisions are already made so they are not
re-litigated.

## Where the code is

| Branch | Repo | State |
|---|---|---|
| `feat/protected-content-phase0` | witchhunt-reader | 9 commits, not pushed |
| `feat/injectable-inflate` | freeink-sdk (submodule) | 1 commit, not pushed, upstream-able to Free-Ink |
| `fix/free-memory-before-wifi` | witchhunt-reader | **merged path: PR #270 open**, unrelated to this work |

The firmware branch's submodule pointer references the SDK commit, so checking out the firmware
branch alone is not enough — `git submodule update` or the SDK branch must come with it.

## What is actually proven

**Phase 0 (cache enciphering) — done and device-validated.**
- 952 host tests green, including 44 that run every corpus book through the whole pipeline twice
  (plain and enciphered) and require identical page dumps with unreadable bytes on disk.
- On an X3: canary book renders, cache unreadable, control case readable, device key persists.
- Flash 93.3%, no measurable change.

**The Wi-Fi memory fix — device-validated, separate PR #270.** Not part of this work; it came
out of the same session because the web-browser path was failing to associate.

## What only compiles

Everything in Phase 1. The firmware links, the flash cost is nil, and **no book behaves
differently**, because nothing calls `openProtectedBook()` yet. Specifically unproven:

- the three decompression loops in `ProtectedBook.cpp`, inverted from push to pull;
- `UzlibContentInflate`, our decompressor behind the SDK's new seam;
- the whole decrypt path, which has never seen an encrypted byte.

The test that would have covered the first two exists (`test/content_inflate`) and **cannot link
on this machine** — see "The linker" below.

## Decisions already made (do not re-open without new information)

1. **Organise by scheme, not vendor.** The device implements Readium LCP and Adobe ADEPT and
   knows nothing about Onleihe, Libby, PNB or BorrowBox. Per-country work is acquisition only,
   which belongs in an SD web plugin. (plan §1.1)
2. **A protected book may be slower and may lose background sectioning** — project owner,
   2026-09-18. If deflate + decipher + write need more memory than the cooperative background
   build can be given, drop the background build for protected books rather than contort the
   read path. Ordinary books keep every optimisation they have (RULE P1). (plan §4)
3. **One inflate implementation in the image** — project owner, 2026-09-18, "the cost of two
   libraries is too big". Hence the SDK seam and our uzlib provider rather than the vendored
   miniz.
4. **The cache cipher is ChaCha20 via wolfSSL**, chosen on measured throughput (4.6 MB/s vs
   1.8 for a portable core, vs 1.2-1.5 for AES-CBC). (plan §3.1)
5. **Hardware AES is not part of this work.** It accelerates only bulk CBC, cannot touch a page
   turn, and a CTR cache would get *slower* through it. (plan §2.4)

## Next actions, in the order I would take them

1. **Retire the LCP risk — 10 lines of Python, no device, no firmware.** Extract
   `META-INF/license.lcpl` from one real Onleihe loan, SHA-256 the passphrase,
   AES-256-CBC-decrypt `user_key.key_check`, check it equals the license `id`. If that fails,
   Phase 2 as written does not work and the whole LCP branch needs re-planning. Everything else
   below is cheap by comparison. (plan §9)
2. **Hook the read path — at `ZipFile`, not at `Epub`.** Upstream hooks two `Epub` read
   functions; that does not transfer, because our `Section` builds its own `ZipFile` and
   `EntryReader` (Section.cpp ~1078) and never calls `readItemContents*`. Eight files use
   `ZipFile` directly. Same lesson as Phase 0: hook the class every read passes through.
3. **`ZipFile::getStoredEntryRange()` must refuse encrypted entries** (ZipFile.h:86). It hands
   back a byte range for a STORED entry so callers can read it undecompressed; for a protected
   entry those bytes are ciphertext, and returning them silently yields garbage rather than an
   error. This is the one trap in the phase.
4. **Route protected books down the extract-to-file branch** `Section` already has
   (`sections/html_<n>.bin`), per decision 2: decrypt once through `decryptToSink` into that
   file, then parse from it with the existing slicing. Phase 0 enciphers that file, so the shape
   that leaks plaintext for upstream is safe for us.
5. **Replace the `.rights` stand-in** in `Epub::initCacheCipher()` with the library's real
   `isProtected()` classification. Today `<book>.epub.rights` means only "encipher this book's
   cache"; after this it means what it says.
6. **Give `HalClock` a monotonic floor** so a loan cannot be extended by setting the clock back.
   Until then `ContentProtection.cpp` enforces what it can and says so in a comment.

## The linker (why a test is parked)

`test/content_inflate` is registered but skipped on MinGW. GNU ld 2.47.20260726 crashes — exit
5, a truncated `0xC0000005` — writing a truncated `.exe` and printing nothing, even under
`-Wl,--verbose`.

What I established, so nobody repeats it:

- a `main()` containing only `InflateReader::init/setReadCallback/deinit`, with **none** of our
  code, crashes it identically;
- `ZipEntryReaderTest` links fine against the same archives;
- every symbol in the failing link is present (checked with `nm` against each archive);
- it reproduces at `-O0/-O1/-O2/-O3`, with and without `-static`, with loose objects and with
  archives, through cmake and by hand;
- there is no second linker on this box (no lld, `clang64` is empty).

`cmake -S test -B build/test -DCROSSPOINT_FORCE_CONTENT_INFLATE_TEST=ON` re-enables it. Try that
first after any toolchain update; the test is written and ready.

## Things that will bite

- **`.rights` currently has two meanings** (see next action 5). Deleting it from a canary book
  makes the book ordinary again — that is correct today and confusing tomorrow.
- **Stale enciphered cache + removed marker** decrypts to noise, fails its header checks and
  rebuilds. Correct, but slow and silent; the same is true of a deleted `.csalt`.
- **The submodule.** The SDK change is a separate commit in a separate repo. A firmware branch
  without it will not compile.
- **`clang-format` on a directory glob** reformats files the change never touched. I did this to
  six SDK headers and had to strip them back out of the commit; format only what you edited.
- **Overlapping PlatformIO builds** clobber `.pio/build/default`. Starting a second build off a
  completion notification is not enough — wait for the process to exit, or the link and the
  objects end up from different runs (it cost an hour here).

## Measurements worth keeping

| | |
|---|---|
| ChaCha20 (wolfSSL) on C3 | 4.60 MB/s |
| ChaCha20 (portable core) | 1.83 MB/s |
| AES-128-CBC decrypt | 1.53 MB/s |
| AES-256-CBC decrypt | 1.21 MB/s |
| Cache cipher, 4 KB page record | 1.11 ms (budget 2 ms) |
| Cache cipher, 48 KB image | ~12.8 ms (budget 25 ms) |
| Flash after linking ContentProtection | 6,117,129 / 6,553,600 (93.3%) |
| Heap free during Wi-Fi association (X3) | 58,400 bytes, after freeing 48 KB |

The last row is the one that explains the Wi-Fi fix: the stack takes essentially everything.

An LCP book's first open pays AES-256-CBC at 1.21 MB/s — roughly 83 ms per 100 KB chapter, once.
Whether that fits the <15% first-open budget needs the section-build baseline from
`runRenderBenchmark`, which is still not measured.
