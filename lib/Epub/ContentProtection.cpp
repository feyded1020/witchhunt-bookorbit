// SD/HAL binding for the content-protection read path.
//
// Ported from crosspoint-reader's feat-sd-plugins branch (lib/Epub/ContentProtection.cpp,
// Justin Mitchell / @itsthisjustin). The flow is his: classify with a ZipScan before touching
// crypto, transfer that same scan into ProtectedBook, prefer an out-of-band rights sidecar, and
// fail closed on a due date with no trustworthy clock. Adapted here for our HAL (HalStorage,
// HalClock) and our heap discipline; see docs/protected-content-plan.md §4 for what each of
// those details is protecting against.
//
// The ContentProtection SDK lib is storage-agnostic (it works against a ByteSource). This file
// is the firmware-side glue that backs that seam with the device's SD storage. It lives in the
// firmware, not the SDK lib, so the portable lib carries no HAL dependency.

#include <Arduino.h>
#include <ByteSource.h>
#include <ContentProtection.h>
#include <Credential.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <ProtectedBook.h>
#include <UzlibContentInflate.h>
#include <WolfsslCrypto.h>
#include <Zip.h>
#include <esp_heap_caps.h>

namespace freeink {
namespace content {

namespace {

// The access credential is provisioned off-device and dropped here. A generic path: the reader
// carries no scheme name, because the same file serves every ADEPT-fulfilled service.
constexpr const char* kCredentialPath = "/.crosspoint/content.key";

// One shared crypto backend for the whole read path.
WolfsslCrypto& crypto() {
  static WolfsslCrypto instance;
  return instance;
}

// The decompressor the library borrows for every protected entry. Injected rather than letting
// it use its own miniz: that would be a second inflate implementation in the image, and miniz
// wants its state in one ~40 KB block — the hardest thing to get on this heap. See
// UzlibContentInflate.h. Static for the same reason crypto() is: it must outlive the
// ProtectedBook that borrows it, and only one book is open at a time.
UzlibContentInflate& contentInflate() {
  static UzlibContentInflate instance;
  return instance;
}

// The decompressor and its scratch want contiguous blocks, and a free-bytes check cannot see
// fragmentation — so this asks the largest block and only then gives something back.
//
// Upstream calls freeink::MemoryManager here; we do not link that SDK lib, and the caches worth
// dropping on this path are ours anyway. Deliberately NOT the framebuffer: the reader is still
// on screen behind a book that may yet open.
void reclaimContentCaches(const GfxRenderer* renderer) {
  constexpr size_t CONTENT_WORKING_SET = 64 * 1024;
  const size_t before = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  if (before >= CONTENT_WORKING_SET) return;
  if (renderer) {
    if (auto* cache = renderer->getFontCacheManager()) cache->clearCache();
  }
  LOG_DBG("CPRO", "Cache reclaim: max_block=%u -> %u, free=%u", static_cast<unsigned>(before),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getFreeHeap()));
}

// ByteSource over an SD file (read-only). One open handle per instance.
class SdByteSource : public ByteSource {
 public:
  explicit SdByteSource(std::string path) : path_(std::move(path)) {}

  bool open() {
    file_ = Storage.open(path_.c_str(), O_RDONLY);
    return file_ && file_.isOpen();
  }

  // Open once and reuse across reads: decrypting a book faults many entries, and reopening per
  // entry would be an SD open per chapter.
  bool ensureOpen() { return (file_ && file_.isOpen()) || open(); }

  int32_t readAt(uint64_t offset, void* dst, uint32_t len) override {
    if (!file_ || !file_.seek64(offset)) return -1;
    return file_.read(dst, len);
  }

  uint64_t size() const override { return file_ ? file_.fileSize64() : 0; }

 private:
  std::string path_;
  mutable HalFile file_;  // mutable so the const size() above can still use it
};

// Adapts an opened ProtectedBook to the reader-facing access interface.
class ProtectedBookDecryptor : public ContentDecryptor {
 public:
  ProtectedBookDecryptor(std::string epubPath, std::unique_ptr<ProtectedBook> book)
      : source_(std::move(epubPath)), book_(std::move(book)) {}

  bool isEncrypted(const std::string& itemPath) const override { return book_->isEncrypted(itemPath); }

  size_t decryptedSize(const std::string& itemPath) const override { return book_->decryptedSize(itemPath); }

  bool decryptToSink(const std::string& itemPath, ContentChunkSink sink, void* context) override {
    if (!source_.ensureOpen()) return false;
    reclaimContentCaches(nullptr);
    if (!book_->decryptEntryToSink(source_, crypto(), itemPath, sink, context)) {
      LOG_ERR("CPRO", "Decrypt failed: %s (%s), free=%u max_block=%u", itemPath.c_str(), book_->lastError().c_str(),
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      return false;
    }
    return true;
  }

 private:
  SdByteSource source_;
  std::unique_ptr<ProtectedBook> book_;
};

}  // namespace

std::unique_ptr<ContentDecryptor> openProtectedBook(const std::string& epubPath, std::string& err) {
  err.clear();

  SdByteSource source(epubPath);
  if (!source.open()) return nullptr;

  // Classify BEFORE initialising crypto or loading credentials, then hand this same ZIP index to
  // ProtectedBook: a protected book scans once rather than twice, and a plain book returns here
  // having paid one scan and nothing else (plan RULE P1).
  ZipScan scan;
  if (!scan.open(source) || !scan.find("META-INF/encryption.xml")) return nullptr;
  reclaimContentCaches(nullptr);

  // A book carrying encryption.xml may only be obfuscating its embedded FONTS, which is an
  // ordinary EPUB feature and not content protection at all. The SDK decides that after parsing
  // the manifest, which is why the credential is not demanded until after openFromScan() and why
  // a false isProtected() below sends the book back down the normal reader path.
  SdByteSource credSource(kCredentialPath);
  Credential credential;
  const bool haveCredential = credSource.open() && parseCredential(credSource, &credential);

  auto book = makeUniqueNoThrow<ProtectedBook>();
  if (!book) {
    err = "out of memory";
    return nullptr;
  }

  // Prefer an out-of-band rights document delivered as a sidecar beside the book
  // ("<book>.epub.rights"), so the EPUB on disk stays byte-identical to what the server sent.
  // Falls back to a rights.xml injected into the zip.
  std::string rightsOverride;
  {
    // A real rights document is a few KB; 64 KB is a generous ceiling. The largest-block check
    // keeps the resize below from aborting: string growth is a bare allocation, and a bare
    // allocation under -fno-exceptions calls abort() rather than returning null.
    constexpr uint64_t kMaxRightsSize = 64 * 1024;
    SdByteSource rightsSource(epubPath + ".rights");
    if (rightsSource.open()) {
      const uint64_t rsize = rightsSource.size();
      if (rsize > 0 && rsize <= kMaxRightsSize &&
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > static_cast<size_t>(rsize) + 8 * 1024) {
        rightsOverride.resize(static_cast<size_t>(rsize));
        const int32_t rn = rightsSource.readAt(0, rightsOverride.data(), static_cast<uint32_t>(rsize));
        if (rn <= 0) {
          rightsOverride.clear();
        } else {
          rightsOverride.resize(static_cast<size_t>(rn));
        }
      }
    }
  }

  book->setInflate(&contentInflate());

  if (!book->openFromScan(source, crypto(), credential, std::move(scan), rightsOverride)) {
    if (haveCredential) {
      err = "cannot open protected content";
      const std::string& detail = book->lastError();
      // Guarded concat: this path runs precisely when the heap is tight, and the temporary
      // would abort under -fno-exceptions.
      if (!detail.empty() && heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) > detail.size() + err.size() + 1024) {
        err += ": ";
        err += detail;
      }
    } else {
      err = "no content access key on this device";
    }
    return nullptr;
  }

  // Font obfuscation only: no protected content here, so let the reader open it normally.
  if (!book->isProtected()) return nullptr;

  // Loan enforcement.
  //
  // DIVERGENCE FROM UPSTREAM, deliberate: they enforce against lib/TrustedTime, a monotonic
  // clock floor persisted in NVS that cannot be rolled back. We have no TrustedTime — HalClock
  // persists the epoch to NVS with drift correction across sleep and knows when it is stale, but
  // nothing stops it moving BACKWARDS, so a user who sets the clock back extends a loan. Closing
  // that needs a monotonic floor in HalClock (plan §4); until then this enforces what it can and
  // the gap is recorded rather than implied.
  //
  // A book with a due date and no plausible clock at all still fails closed.
  if (book->expiresAt() != 0) {
    const int64_t now = static_cast<int64_t>(HalClock::now());
    if (now == 0 || HalClock::isStaleRestore()) {
      err = "loan date unverified";
      return nullptr;
    }
    if (book->isExpired(now)) {
      err = "access expired";
      return nullptr;
    }
  }

  auto decryptor = makeUniqueNoThrow<ProtectedBookDecryptor>(epubPath, std::move(book));
  if (!decryptor) err = "out of memory";
  return decryptor;
}

}  // namespace content
}  // namespace freeink
