# Protected content: LCP (Onleihe/CARE and peers) and Adobe ADEPT (Libby, BorrowBox)

Status: written 2026-09-18. **Phase 0 is implemented** (see the status box in §3); Phases 1-4
are not. The point of the document is to fix the order of work, the performance contract it
must hold to, and the alternatives that were considered and rejected.

The goal is to read books the user has legitimately borrowed or bought — their own loan,
their own passphrase or credential. The read path decrypts on demand into memory; nothing
decrypted is written to the card in the clear (§3 is entirely about honouring that without
losing the caches the reader is built on).

**Two schemes, not two vendors.** This is the organising idea of the whole plan: the device
implements Readium LCP and Adobe ADEPT, and knows nothing about Onleihe, Libby, PNB or
BorrowBox. Everything vendor-specific — logging in, listing loans, fetching a file — lives
off the device (§6). That is what turns "support Onleihe" into "support the LCP ecosystem"
for no extra firmware, and it is why §1.1 is worth reading before §3.

---

## 1. Two schemes, one read path

| | LCP (Onleihe/CARE, PNB, …) | Adobe ADEPT (Libby/OverDrive, BorrowBox, Kobo, Google Play) |
|---|---|---|
| Scheme | Readium **LCP** (`META-INF/license.lcpl`) | Adobe **ADEPT** (`META-INF/rights.xml`) |
| Content key | passphrase → SHA-256 → AES-256-CBC unwrap | RSA-unwrapped from an account credential |
| Entry cipher | AES-256-CBC, IV = first 16 bytes | AES-128-CBC, IV = first 16 bytes |
| Acquisition | download the publication; no activation server | ACSM fulfilment against an activated Adobe ID |
| Specification | published | reverse-engineered |
| Device read code | new, small (§5) | **already in the tree**, unwired (§4) |
| Off-device piece | none for the MVP | an activation/credential generator that exists nowhere (§7) |

`freeink-sdk/libs/book/ContentProtection` is the ADEPT read path: `Rights.cpp` parses
`rights.xml`, `ProtectedBook.cpp` unwraps the content key and decrypts entries on read,
`Credential.cpp` reads the off-device `FREEINK-CONTENT-KEY 1` bundle, and `WolfsslCrypto.cpp`
supplies the primitives. It is present in our checkout and **not** referenced from
`platformio.ini` or `src/`. Its `ContentDecryptor` interface (`include/ContentProtection.h`)
is scheme-neutral, so LCP becomes a second implementation behind the same interface rather
than a parallel reader.

### 1.1 Reach: what each scheme unlocks

Onleihe's CARE is an LCP implementation, and LCP is an interoperable standard with an
identical container format wherever it is used. So Phase 2 is not an Onleihe feature that
might later be generalised — it is the whole LCP ecosystem the day it works, with European
public-library lending as the centre of gravity.

| Region | Service | Scheme | Status |
|---|---|---|---|
| DE / AT / CH | Onleihe (divibib), CARE | LCP | the driving case; verify on a real loan (§9) |
| FR | PNB / Dilicom-based library lending | LCP | **verify** with one loan |
| ES | eBiblio (Odilo) | LCP | **verify** |
| DK / SE / NO | eReolen, Biblio, BookBites and peers | LCP, adoption varies per country | **verify each** |
| IT / NL / BE | MLOL, online-bibliotheek | mixed LCP and Adobe | **verify each** |
| AU / UK / IE | BorrowBox (Bolinda) | Adobe ADEPT | covered by Phase 4, no extra work |
| Worldwide | Libby/OverDrive, Kobo, Google Play | Adobe ADEPT | covered by Phase 4, no extra work |

Every "verify" above is one command on one real loan — `unzip -l book.epub` — and costs
nothing to check. Do not write code against an assumed scheme; several of these services ship
both, sometimes per title.

**What varies between LCP platforms, and where it lands:**

| Varies | Lands in |
|---|---|
| Login, loan list, download URL | a per-service web plugin (§6), zero firmware |
| Where the passphrase comes from (birthdate, account password, card number) | the license's own `user_key.text_hint`, which we display — no per-service code |
| Loan length, renewal, early return | the License Status Document (§5), one protocol for all of them |
| Certificate chain / provider identity | one LCP root, one code path |

**What does not vary at all for ADEPT:** the device sees `rights.xml` plus the user's
credential, and has no idea whether the book came from Libby, BorrowBox, Kobo or Google Play.
Vendor differences are entirely in fulfilment, which is off-device (§7). Phase 4 therefore
covers every ADEPT service at once, or none of them.

---

## 2. Performance contract (normative)

Two properties, both testable. Everything in §3–§5 is shaped by them.

### RULE P1 — a book without protection pays nothing measurable

No new allocation, no new virtual dispatch on any existing hot path, and no extra SD
operation per page. What a plain book may pay:

* **One batched central-directory lookup at open.** Detection asks for
  `META-INF/encryption.xml`, `META-INF/license.lcpl` and `META-INF/rights.xml` — these join
  the existing batch scan (`ZipFile::fillFileStats`, [ZipFile.h:99](../lib/ZipFile/ZipFile.h#L99)),
  they do not add three new walks of the central directory.
* **One null check per entry read and per cache file operation.** `if (decryptor)` and
  `if (cipher_)`, both predictable branches on a pointer that is null for every plain book.

What it must not pay: an `FsFile` wrapper installed globally, a virtual read interface
threaded through `Storage`, or a cipher object constructed for books that do not need one.
The filter is installed per book at open time, and a plain book never constructs it.

### RULE P2 — the cache cipher is length-preserving and offset-addressable

The reader seeks into its caches everywhere: page loads are `seek(lut[currentPage])` plus one
page record ([Section.cpp:1856](../lib/Epub/Epub/Section.cpp#L1856)), the section header is
patched in place ([Section.cpp:1541](../lib/Epub/Epub/Section.cpp#L1541)), and the anchor and
page-break maps are read by absolute offset. A cipher that changes length, or that requires
sequential decryption from byte zero, would invalidate every one of those offsets and turn a
few-KB page load into a whole-file read.

So: a **stream cipher keyed by byte offset** — ChaCha20 (block counter = `offset / 64`) or
AES-CTR (counter = `offset / 16`). Ciphertext length equals plaintext length, every existing
offset stays valid, and a page turn decrypts exactly the bytes it reads. CBC is excluded for
the cache for this reason; it remains mandatory for the *book* entries, where the DRM scheme
dictates it.

### RULE P3 — pick the cipher by measurement, on today's software crypto

Our wolfSSL build runs **100% software crypto**: the Espressif HW port files
(`esp32_aes.c`, `esp32_sha.c`, `esp32_mp.c`) are gated on `WOLFSSL_ESPIDF`, which only
auto-enables under ESP-IDF, and our build is Arduino — those objects compile to zero text
symbols today. Plan the design so it is correct and fast enough without an accelerator;
treat hardware AES as an optimisation to be measured afterwards (§2.4), not a premise.

ChaCha20 is the default choice for the cache: markedly cheaper than table-driven AES on a
32-bit core, and `HAVE_CHACHA` is already defined by `scripts/patch_wolfssl.py`, so it adds
no dependency and little flash. Confirm with the device micro-benchmark before committing
(§3, step 1).

### 2.4 Hardware AES: what it buys, and what it costs

The ESP32-C3 and S3 both have an AES peripheral, and Arduino-wolfSSL ships a port for it, so
this is worth costing rather than assuming. Reading
`.pio/libdeps/default/Arduino-wolfSSL/src/wolfcrypt/src/port/Espressif/esp32_aes.c`:

* **It accelerates bulk CBC, and only bulk CBC.** `wc_esp32AesCbcDecrypt` takes the hardware
  mutex once, sets the key once, then loops every block. `wc_esp32AesEncrypt` — the
  single-block call that CTR mode is built from — takes the mutex, reloads the key mode, does
  **one 16-byte block**, and releases. A CTR cache cipher driven through that path would pay a
  FreeRTOS mutex and a key reload per 16 bytes, which is almost certainly slower than software
  ChaCha20. This is the decisive argument for keeping the cache on ChaCha20 (RULE P2 already
  ruled out CBC for the cache, because it is neither length-preserving nor seekable).
* **So the win lands exactly on the DRM path**: the AES-CBC entry decryption the formats
  mandate — AES-128 for ADEPT, AES-256 for LCP, both supported in C3/S3 hardware (192 is not,
  and neither scheme uses it).
* **But that path runs once per entry**, during the section build, alongside inflate and
  parse. Whatever hardware AES saves, it saves on a one-time cost that the cache then hides.
  It cannot improve a page turn, because a page turn reads the cache, not the book.
* **Enabling it is a TLS-stack change, not a local one.** It needs `WOLFSSL_ESPIDF` plus
  `WOLFSSL_ESP32_CRYPT`, and `WOLFSSL_ESPIDF` is a broad switch: it also pulls in
  `esp32_util.c` and the `esp_sdk_*_lib.c` shims and includes `sdkconfig.h`. The blast radius
  is containable — `NO_WOLFSSL_ESP32_CRYPT_HASH` and `NO_WOLFSSL_ESP32_CRYPT_RSA_PRI` keep
  SHA and bignum on the software code we ship today, which is the part previous investigation
  found risky on C3 — but it still means re-validating TLS (handshake, OTA, KOSync) for a
  gain on one non-interactive path.
* **Contention is a real runtime cost.** The peripheral is shared under a mutex with a
  5000-tick wait. A protected book being built while the web server is serving TLS would
  serialise the two on the AES block; today they are independent software paths.
* The C3 DMA mode is marked "not yet implemented" in the port, so this is register/block
  mode — good for a few hundred KB, not a bulk engine.

**Position**: do not enable it as part of this work. Measure software AES-CBC on device
first (§3, step 1); if the first-open budget in the table below is met — and it very likely is,
since parse dominates — hardware AES buys nothing a user can feel, at the price of touching
the TLS stack. Revisit it as a standalone change, justified by the TLS record path rather
than by DRM, and measured against the 7.7 s handshake baseline already on record.

### Budgets, and how they are checked

SD reads at 20 MHz SPI ([HalStorage.cpp:77](../lib/hal/HalStorage.cpp#L77)) give roughly
1.5–2.5 MB/s in practice; an e-ink page refresh costs several hundred ms. The cipher is only
allowed to matter where it would be a large fraction of a small number.

| Path | Volume | Budget |
|---|---|---|
| Page turn, protected book | one page record, single-digit KB | **< 2 ms** (under 1% of a refresh) |
| Section build, cache write | whole section text, once | **< 10%** over the same book unprotected |
| Image display from cache | one converted bitmap (~48 KB full-screen 1-bit) | **< 25 ms** |
| First open, book entries | AES-CBC per entry, once, alongside inflate + parse | **< 15%** over the same book unprotected |
| Plain book, any path | — | **0**, within measurement noise |

Instruments, both already in the tree:

* `EpubReaderActivity::runRenderBenchmark()` under `ENABLE_BENCHMARKS`
  ([EpubReaderBenchmark.cpp](../src/activities/reader/EpubReaderBenchmark.cpp)) reports
  forward/backward ms per turn plus section-load, page-load and render phases. A/B the same
  book plain vs protected for the budgets, and A/B master vs branch on a plain book for P1.
* The host pipeline goldens. The cache *format* does not change — only whether its bytes are
  enciphered at rest — so every existing golden must stay byte-identical. That is the
  regression net proving the plain path was not touched.

---

## 3. Phase 0 — the cache at rest (blocker, shared by both schemes)

> **Status 2026-09-18.** Step 2 is implemented: [lib/CacheCipher](../lib/CacheCipher) with
> `test/cache_cipher` covering the RFC 8439 vectors and the random-access properties (8 tests,
> green; the keystream was also cross-checked against `openssl enc -chacha20`, and the full
> host suite is 895/895). Step 1 has a tool that builds clean — `pio run -e bench_crypto -t
> upload`, then `pio device monitor -e bench_crypto` — but **no numbers yet**: it needs a
> device. **Step 1 is measured — see §3.1.** Steps 2, 3 and 4 are implemented:
> `lib/CacheCipher` (cipher + derivation, host-tested), `lib/hal/CacheKeyStore` (device key in
> NVS, salt per cache directory), and the install point — `HalStorage::setCacheCipherScope()`,
> which enciphers every file opened under a book's cache directory, with `Epub` owning the
> scope for the book's lifetime.
>
> Detection is still a stand-in: a `<book>.epub.rights` sidecar marks a book protected. That is
> the same marker Phase 1 uses, so it is the seam the real classification replaces — and it
> lets the mechanism be exercised today (see `test/device/README.md`).
>
> Verified: 952 host tests green, including 44 cases that run the whole pipeline over an
> enciphered cache and require identical page dumps plus unreadable bytes on disk. Firmware
> flash 93.3%, within noise of the 93.5% baseline.
>
> One answer also fell out of wiring the benchmark: AES-CTR is **not** in our TLS profile
> (`scripts/patch_wolfssl.py` enables AES-GCM and ChaCha20-Poly1305 only), so picking AES-CTR
> for the cache would cost flash that ChaCha20 does not, since `HAVE_CHACHA` is already on.
> AES-CBC *is* available, which is what §4 needs for the book's own entries.

### 3.1 Step 1 results (ESP32-C3, measured 2026-09-18)

Software crypto throughout, flat from 2 KB to 64 KB — per-call setup is negligible, so these
are throughput numbers and nothing else:

| Cipher | MB/s | 4 KB page record | 48 KB image |
|---|---|---|---|
| wolfSSL ChaCha20 | **4.60** | 0.89 ms | ~10 ms |
| Portable ChaCha20 (our first cut) | 1.83 | 2.25 ms | ~26 ms |
| wolfSSL AES-128-CBC decrypt | 1.53 | 2.69 ms | ~31 ms |
| wolfSSL AES-256-CBC decrypt | 1.21 | 3.38 ms | ~40 ms |
| wolfSSL AES-256-CTR | 1.19 | 3.44 ms | ~41 ms |

**Decision: wolfSSL's ChaCha20 backs the device path.** It is the only candidate that meets
both budgets (2 ms per page turn, 25 ms per cached image), it is already linked for TLS so it
adds no flash, and it is 2.5x our own first cut — the gap being a byte-at-a-time XOR against a
word-wise one. `lib/CacheCipher` keeps the portable core for host tests, where the RFC vectors
live and wolfSSL cannot be built; the device path is pinned to the same vector by
`bench_crypto`, so neither side is taken on trust.

**Consequence for §4, and it reopens §2.4.** AES-256-CBC at 1.21 MB/s means an LCP book pays
roughly 0.8 ms per KB of chapter text on first open — a 100 KB chapter is ~83 ms, once, beside
inflate and parse. Whether that fits the < 15% first-open budget depends on the section-build
baseline, which is **not yet measured**: get it from `runRenderBenchmark` before Phase 2 is
scheduled. If it does not fit, hardware AES is the lever after all, because bulk CBC is
precisely what the Espressif port accelerates (§2.4) — and note ADEPT's AES-128 is 26% faster
than LCP's AES-256, so the two schemes do not have the same first-open cost.

Today a protected book would leave its entire plaintext on the card beside the encrypted
file. The per-book cache dir `epub_<hash>/` holds `sections/html_<n>.bin` (the extracted
XHTML verbatim, [Section.cpp:299](../lib/Epub/Epub/Section.cpp#L299)), `sections/<n>.bin`
(parsed and laid-out pages), `img_*` (extracted images), footnote previews and
`pagelist.bin`. This is solved first; it is not a finishing touch.

1. **Micro-benchmark first.** On device (C3 on COM6, and the S3 board), 4 KB and 64 KB
   buffers, reported in MB/s:

   | Measurement | Decides |
   |---|---|
   | ChaCha20, software | the cache cipher (RULE P2/P3) |
   | AES-CTR, software | whether one primitive fewer is affordable |
   | AES-128-CBC and AES-256-CBC, software | the first-open budget, and whether §2.4 is moot |
   | AES-CBC with the wolfSSL ESP32 port enabled (optional probe) | what hardware would actually buy |

   The last row is a throwaway build, not a commitment — it exists so §2.4 is decided by a
   number rather than by assumption. Report the 4 KB figures separately: per-call overhead,
   not steady-state throughput, is what a page turn pays.
2. **`CacheCipher`**: keystream at an absolute file offset, plus
   `encryptInPlace(buf, len, offset)` / `decryptInPlace(...)`. No file-format knowledge, no
   allocation per call, host-testable.
3. **Key management.** One device cache key in NVS; the per-book key is
   `SHA-256(deviceKey || cacheDirName)`. This avoids an NVS entry per book (the NVS partition
   is 20 KB) and makes disposal automatic: delete the cache dir and the derived key is
   meaningless. Clear Cache already removes `epub_`/`xtc_`/`txt_` dirs, so there is no new
   lifecycle to invent.
4. **Install point.** The book's cache handle carries an optional `CacheCipher*`, null for
   plain books (RULE P1). Reads and writes in `Section`, the footnote store and the image
   converters pass their file offset to the cipher; because the cipher preserves length, no
   caller's offset arithmetic changes.

The alternative — simply not caching for protected books — is rejected: pagination is built
on `sections/<n>.bin` and `pagelist.bin`, so it is a reader rewrite rather than a switch, and
it would make protected books dramatically *slower* than plain ones, which is what the
performance contract exists to prevent.

**There is nothing to port here, which is worth stating explicitly** (checked 2026-09-18).
Upstream's `feat-sd-plugins` has no cache cipher: the only thing resembling one in that tree
is `lib/Serialization/ObfuscationUtils`, the same MAC-XOR credential helper we already have.
Their `Section.cpp` contains no protection awareness at all, and their section build streams
the decrypted chapter to a temp file on the card
(`readItemContentsToStream(localPath, tmpHtml, 8192)`) before parsing it, while the laid-out
pages it produces hold the book's words. So their `docs/sd-plugins.md` line "nothing
decrypted is ever written to SD" describes the entry-read path, not the build path — and
Phase 0 is filling a genuine gap rather than duplicating their work. Worth raising upstream
if we ever offer this back.

---

## 4. Phase 1 — shared decrypt-on-read

* Build the SDK lib with `-DFREEINK_CONTENT_WOLFSSL=1` and `CONTENT_EXTERNAL_MINIZ` (bind to
  our already-linked miniz; two copies in one image corrupt inflate — see the lib's
  `ContentMinizConfig.h`).
* **Port upstream's adapter rather than writing one.** `lib/Epub/ContentProtection.cpp` on
  `feat-sd-plugins` is 195 lines and carries details that are cheaper to copy than to
  rediscover:
  - **A book can carry `META-INF/encryption.xml` and not be protected at all** — EPUB font
    obfuscation uses the same manifest. Their flow classifies with a `ZipScan`, hands that
    same scan to `ProtectedBook`, and falls back to the normal reader path when
    `isProtected()` comes back false. Detection that treats `encryption.xml` as "this book is
    DRM'd" would misfire on ordinary books with obfuscated fonts; this is the trap to copy
    around.
  - Classification happens **before** crypto init or credential loading, so a plain book pays
    one scan and nothing else (our RULE P1).
  - `reclaimContentCaches()` frees caches when the largest free block is under a 64 KB
    working set, because miniz needs contiguous memory for the inflate state.
  - The rights sidecar `<book>.epub.rights` is read with a 64 KB cap **and** a
    largest-free-block check before the string resize — bare allocation under
    `-fno-exceptions` aborts rather than returning null.
  - Error strings (`"loan date unverified"`, `"access expired"`) are matched by the reader to
    pick the user-facing message; keep them if we keep their reader wording.
  The adapter binds `ByteSource` to HalStorage and `Crypto` to the wolfSSL backend, opens the
  book once, and keeps the decryptor for the session.
* Hook the two `Epub` entry-read paths so an encrypted entry routes through `decryptToSink`,
  each gated on `if (decryptor && decryptor->isEncrypted(path))`.
* **Hook `ZipFile`, not the `Epub::readItem*` helpers.** Upstream hooks two read functions on
  its own `Epub`, which does not transfer: our `Section` constructs its own `ZipFile` and
  `EntryReader` (Section.cpp ~1078) and never calls `readItemContents*`, so hooking those four
  helpers would miss the main text path entirely. Eight files use `ZipFile` directly. Same
  lesson as Phase 0 — the choke point is the class every read passes through.
* **The streaming tension, and how to settle it.** `ContentDecryptor::decryptToSink()` decrypts
  AND inflates a whole entry in one call. Our parse path is the opposite shape: `EntryReader`
  steps through ~1 KB slices out of a `BuildArena`, yielding to the UI and honouring
  cooperative aborts between slices. A whole-entry sink cannot be paused mid-entry.
  Rather than write a second streaming AES+inflate path, route a protected book down the
  extract-to-file branch Section already has (the `sections/html_<n>.bin` bank): decrypt the
  entry once through the sink into that file, then parse from it with the existing slicing.
  Upstream does exactly this and lands the plaintext on the card; for us Phase 0 enciphers that
  file, so the same shape is safe here and is not for them. Cost: a protected book always pays
  the extract step on first open, never on a cache hit.

  **Decided (project owner, 2026-09-18): a protected book may be slower and may lose background
  sectioning.** If deflate + decipher + write together need more memory than the cooperative
  background build can be given, drop the background build for protected books rather than
  contort the read path around it. This is a deliberate two-tier reader: ordinary books keep
  every optimisation they have today (RULE P1 is untouched), and a protected book trades
  smoothness for working at all. Design to that rather than treating a regression on protected
  books as a defect — but say so in the UI if a first open becomes long enough to look hung, and
  measure what it actually costs before assuming it must be paid.
* **`ZipFile::getStoredEntryRange()` must refuse encrypted entries.** It returns a byte range
  for a STORED entry so callers can read it with no decompression
  ([ZipFile.h:86](../lib/ZipFile/ZipFile.h#L86)); for a protected entry those bytes are
  ciphertext, and handing them back silently yields garbage instead of an error. This is the
  one trap in the phase.
* Expiry: `Rights::expiresAt` is enforced by the caller. `HalClock` already persists the epoch
  to NVS with drift correction across sleep and staleness detection; what it lacks is a
  monotonic floor so a due date cannot be walked backwards by resetting the clock. That is an
  addition to `HalClock`, not a new library.

---

## 5. Phase 2 — LCP, the half that can be entirely ours

Written against the format, never against a service. Nothing in this phase may contain the
string "Onleihe": every behaviour comes out of the license document, which is how the same
code serves PNB, eBiblio and the Nordic platforms (§1.1).

* `LcpLicense`: parse `META-INF/license.lcpl`, derive the user key as SHA-256 of the
  passphrase, AES-256-CBC-unwrap `encryption.content_key`, and verify against
  `user_key.key_check` — it must decrypt to the license `id`. A wrong passphrase is then a
  clean "wrong passphrase", not a corrupt book.
* **Check `encryption.profile` first, and treat it as a gate.** The basic profile is openly
  documented; the production profile (`…/lcp/profile-1.0`) is the one real platforms issue,
  and its key-derivation details are not public in the way the basic profile's are. Before
  any of this is scheduled, run the key-check step against a real license offline (§9) — if
  the derivation does not reproduce `key_check`, Phase 2 as written does not work and the
  whole LCP branch needs re-planning. This is the single largest technical risk in the
  document, and it is cheap to retire: it needs one license and no firmware.
* **Display `user_key.text_hint`** in the prompt, verbatim. This is what makes the feature
  multi-platform without per-platform code: one service's hint says "date of birth,
  DD.MM.YYYY", another's says "your library card number". A prompt that hardcodes a format
  serves exactly one country.
* Entry decryption is AES-256-CBC with the IV as the first 16 bytes, honouring `compression`
  and `original_length` from `encryption.xml` (LCP stores entries deflated *then* encrypted,
  with the zip method typically 0).
* `Crypto` gains `aes256CbcDecrypt` (a wolfSSL one-liner); `ProtectedBook` is AES-128 only
  today (`bookKey_[16]`), so either widen it or add an `LcpBook` behind the same
  `ContentDecryptor` interface.
* Passphrase entry reuses
  [KeyboardEntryActivity](../src/activities/util/KeyboardEntryActivity.h). A date of birth
  needs only the numeric layout, so the German case does not touch the keyboard-layout flash
  problem; services whose hint asks for an account password need the full layout, so check
  what that costs before promising them (see `docs/sd-plugins.md` on the keyboard split).
* Cache the **verified content key**, never the passphrase, and never on the card in the
  clear: write it into the book's cache dir enciphered with the Phase-0 device key. Deleting
  the cache re-prompts; removing the card yields nothing.
* Expiry comes from the license `rights.end`, enforced as in §4.
* **Signature and provider certificate.** A faithful client verifies the license signature
  against the LCP certificate chain. One embedded root, a date check, and a clear refusal on
  failure; the cost is small and it is what keeps "we implement the licensed read path"
  literally true rather than approximately true. Certificate revocation lists are out of
  scope for a device that is usually offline — say so in the UI wording rather than pretending.
* **License Status Document, deferred but designed for.** `links[rel=status]` is how LCP
  expresses return, renewal and revocation, and `rights.end` can be updated by it. The MVP
  ignores it, which means an early return or a revoked loan is not noticed until the license
  expires on its own terms. Keep the license document addressable by id so a later phase can
  fetch its status when the device happens to be online, and record this gap in the release
  notes rather than discovering it in a bug report.
* `links[rel=hint]` is a passphrase-recovery URL. We cannot open a browser; show the host as
  text so the user knows where to go.

After this phase every LCP platform works with **no network code at all**: copy the book to
the card, answer the hint once, read. A new country is then a plugin (§6) or nothing at all.

---

## 6. Phase 3 — acquisition convenience (optional, browser-side, one plugin per service)

The shape is the same everywhere: log in, list loans, fetch the `.lcpl`, follow
`links[rel=publication]`, inject the license into the zip using the JSZip that page already
loads, and POST to `/upload`. Only the first two steps differ per service, and they differ a
lot — OAuth here, a library-card form there, a national aggregator in between.

That is exactly why this belongs in the plugin system and not in firmware. Each country's
service is a folder on the SD card: no flash cost, no build, no release coupling, and a
service that changes its API breaks one plugin instead of a firmware version. The device
already ships everything the plugin needs except outbound POST.

* **A service whose endpoints send CORS needs zero firmware change today.**
* **A service that does not** needs relay v2 (POST with request headers) — the same upgrade
  the rest of the plugin catalog wants, see [sd-plugins.md](sd-plugins.md). Expect most
  library APIs to fall here, since they are built for apps, not browsers.
* Credentials stay in the browser and out of the firmware either way. The device never learns
  a library password; it only ever sees a finished EPUB and a license.

Start with one service end to end (the one whose loans we can actually test), and only then
decide whether the second is worth writing — or whether it is better published to the plugin
catalog for someone in that country to maintain.

---

## 7. Phase 4 — Adobe ADEPT (Libby/OverDrive, BorrowBox, Kobo, Google Play)

Reading is already covered by Phases 0 + 1, for every one of these services at once: the
device sees `rights.xml` and a credential, and never learns which shop or library the book
came from. The credential is per Adobe ID, not per vendor, so one activation covers all of
them — and conversely, if activation is not solved, none of them work.

What is missing is *fulfilment* (ACSM → EPUB) and *activation* (an Adobe ID → device RSA key
and certificate). Three routes:

1. **Off-device, recommended.** The user fulfils on a computer (Adobe Digital Editions, or
   open-source libgourou/knock) and copies the DRM'd EPUB plus a `content.key` bundle to the
   card. The bundle format is documented in the SDK's `Credential.h`, but no generator ships
   anywhere — we would write a ~200-line converter. Off-device, so no flash and no RAM.
2. **Install the existing third-party plugin.** "Protected Content" (listed in the public
   catalog, served from a closed host) needs `api.crypto`, relay v2, Range support in
   `/api/fetch` and the job queue — the whole superset API, for one closed-source blob.
3. Write our own fulfilment client. Reverse-engineering ADEPT ourselves. No.

---

## 8. Rejected alternatives

* **CBC for the cache.** Pads, shifts every stored offset, forces sequential decryption —
  violates RULE P2 and turns a few-KB page load into a whole-file read.
* **No cache for protected books.** A reader rewrite, and it makes protected books much
  slower than plain ones (§3).
* **A virtual read interface on `Storage` so the cipher can be layered generically.** Puts a
  vtable in the path of every plain-book read to serve the rare case — violates RULE P1.
* **Enabling wolfSSL's hardware AES as part of this work.** It accelerates only bulk CBC —
  the one-time DRM entry decryption — and cannot touch a page turn; a CTR cache cipher would
  actually get *slower* through it (a mutex and key reload per 16-byte block). Turning it on
  means enabling `WOLFSSL_ESPIDF` and re-validating TLS for a gain on a non-interactive path.
  Kept as a separate, separately justified change (§2.4).

---

## 9. Verify before building

In rough order of what would hurt most if assumed wrongly.

1. **Does the key derivation reproduce `key_check` on a real production license?** Extract
   `license.lcpl` from one real loan, SHA-256 the passphrase, AES-256-CBC-decrypt
   `user_key.key_check`, and check it equals the license `id`. Ten lines of Python, no
   firmware, no device. **If this fails, Phase 2 does not work as written** and the LCP branch
   needs re-planning before anyone schedules it (§5, `encryption.profile`). Everything else on
   this list is cheap by comparison — do this one first.
2. **Which scheme does each service actually issue?** Several ship both Adobe ACSM and LCP,
   sometimes per title. `unzip -l book.epub | grep -E "license.lcpl|rights.xml"` on one real
   loan per service decides whether it lands in Phase 2 (ours) or Phase 4 (Adobe, with the
   credential problem). Repeat per platform in §1.1 rather than generalising from the German
   case; a single counter-example changes which phase pays for that country.
3. **The passphrase hint.** Read `user_key.text_hint` on licenses from more than one service.
   It is what the prompt shows, and it is the evidence for whether the numeric keyboard is
   enough or the full layout is needed (§5).
4. **Status document reach.** Note whether the licenses carry `links[rel=status]` and what
   their `rights.end` looks like in practice — that sizes the deferred LSD work and tells us
   how wrong the "expiry only" MVP will feel to a user who returns a book early.
5. **Flash.** `.pio/build/default/firmware.bin` was 6,130,192 of 6,553,600 bytes (93.5%) on
   2026-09-18, leaving ~413 KB. Measure the image after Phase 1, before starting Phase 2.
