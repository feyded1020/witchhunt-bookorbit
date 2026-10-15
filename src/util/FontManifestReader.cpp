#include "FontManifestReader.h"

#include <FontManifestParser.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

// The reader's working state, on the heap while read() runs and freed on return: the parser (its
// 512-byte token buffer, the base URL and one file name), the two passes' builders, the open family's
// name and first bad file name, which wait for the family to close, and the read block (one SD sector
// per read). About 1.9 KB, too much for the stack.
struct FontManifestReader::Work {
  Work(const FontManifestCallbacks& callbacks, FontCatalog& catalog) : parser(callbacks), filler(catalog) {}

  FontManifestParser parser;
  // Pass 1 counts what the families that pass their checks hold; pass 2 writes them into the block.
  // FontManifestReader::builder points at one of them while its pass runs.
  FontCatalog::Builder counter;
  FontCatalog::Builder filler;
  char familyName[FAMILY_NAME_BUF_SIZE] = {};
  size_t familyNameLen = 0;
  bool familyNameOverflow = false;
  bool badFile = false;
  char badFileName[FontManifestParser::FILE_NAME_BUF_SIZE] = {};
  char block[512] = {};
};

namespace {

// Copies what fits, NUL-terminated, and returns how much that was.
size_t copyBounded(char* dst, const size_t dstSize, const char* src, const size_t srcLen) {
  const size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
  return n;
}

}  // namespace

FontManifestStatus FontManifestReader::read(HalFile& file, FontCatalog& catalog, std::string& baseUrl) {
  catalog.clear();
  baseUrl.clear();
  failureReason = nullptr;
  manifestVersion = 0;

  FontManifestCallbacks callbacks{};
  callbacks.ctx = this;
  callbacks.onFamilyBegin = [](void* ctx) { static_cast<FontManifestReader*>(ctx)->familyBegin(); };
  callbacks.onFamilyName = [](void* ctx, const char* v, size_t n) {
    static_cast<FontManifestReader*>(ctx)->familyName(v, n);
  };
  callbacks.onFamilyDescription = [](void* ctx, const char* v, size_t n) {
    static_cast<FontManifestReader*>(ctx)->familyDescription(v, n);
  };
  callbacks.onFile = [](void* ctx, const FontManifestFile& f) { static_cast<FontManifestReader*>(ctx)->fileEntry(f); };
  callbacks.onFamilyEnd = [](void* ctx) { static_cast<FontManifestReader*>(ctx)->familyEnd(); };

  const auto state = makeUniqueNoThrow<Work>(callbacks, catalog);
  if (!state) {
    LOG_ERR(logTag, "OOM: font manifest parser (%u bytes)", static_cast<unsigned>(sizeof(Work)));
    failureReason = "out of memory";
    return FontManifestStatus::OutOfMemory;
  }
  // work and builder point into `state`, which this call owns: nothing may hold them after it.
  struct Detach {
    FontManifestReader& reader;
    ~Detach() {
      reader.work = nullptr;
      reader.builder = nullptr;
    }
  } detach{*this};
  work = state.get();

  // Pass 1: is this a manifest we read, and how much do the families that pass their checks hold?
  const FontCatalog::Builder& counter = work->counter;
  builder = &work->counter;
  filling = false;
  if (!feedFile(file)) return FontManifestStatus::Invalid;
  manifestVersion = work->parser.version();
  // v1 (legacy, no crc32) and v2 (with crc32) are both read; the crc check is skipped per file when
  // the field is absent. See upstream PR #1904 and scripts/generate-font-manifest.py.
  if (manifestVersion != 1 && manifestVersion != 2) return FontManifestStatus::UnsupportedVersion;

  const FontCatalog::Size size = counter.size();
  if (!FontCatalog::fits(size)) {
    LOG_ERR(logTag, "Font list too large: %u families, %u files, %u string bytes", static_cast<unsigned>(size.families),
            static_cast<unsigned>(size.files), static_cast<unsigned>(size.stringBytes));
    failureReason = "font list too large";
    return FontManifestStatus::OutOfMemory;
  }
  if (!catalog.allocate(size)) {
    LOG_ERR(logTag, "OOM: font list (%u bytes)", static_cast<unsigned>(FontCatalog::blockBytesFor(size)));
    failureReason = "out of memory";
    return FontManifestStatus::OutOfMemory;
  }

  // Pass 2: the same events again, into the block.
  const FontCatalog::Builder& filler = work->filler;
  builder = &work->filler;
  filling = true;
  work->parser.reset();
  if (!feedFile(file)) {
    catalog.clear();
    return FontManifestStatus::Invalid;
  }
  if (filler.overflowed() || !(filler.size() == size)) {
    catalog.clear();
    failureReason = "changed between passes";
    return FontManifestStatus::Invalid;
  }
  baseUrl = work->parser.baseUrl();
  return FontManifestStatus::Ok;
}

// The whole file, from its start. False, with the reason set, when it cannot be read or is not a
// complete manifest document.
bool FontManifestReader::feedFile(HalFile& file) {
  FontManifestParser& parser = work->parser;
  if (!file.seekSet(0)) return fail("read error");
  for (;;) {
    const int n = file.read(work->block, sizeof(work->block));
    if (n < 0) return fail("read error");
    if (n == 0) break;
    parser.feed(work->block, static_cast<size_t>(n));
  }
  if (parser.hasError()) return fail("not valid JSON");
  if (!parser.complete()) return fail("incomplete document");
  // A cut base URL would send every download to the wrong place.
  if (parser.baseUrlOverflow()) return fail("baseUrl too long");
  return true;
}

void FontManifestReader::familyBegin() {
  builder->beginFamily();
  work->familyName[0] = '\0';
  work->familyNameLen = 0;
  work->familyNameOverflow = false;
  work->badFile = false;
  work->badFileName[0] = '\0';
}

// The last one given wins.
void FontManifestReader::familyName(const char* value, const size_t len) {
  work->familyNameLen = copyBounded(work->familyName, sizeof(work->familyName), value, len);
  work->familyNameOverflow = work->familyNameLen < len;
}

void FontManifestReader::familyDescription(const char* value, const size_t len) { builder->description(value, len); }

// Each file is checked as it comes, in both passes: the counting pass must leave out what the filling
// pass will leave out.
void FontManifestReader::fileEntry(const FontManifestFile& f) {
  // Past a bad file the family is left out whole; only the first is reported.
  if (work->badFile) return;
  if (f.nameOverflow || !fileNameOk(f.name)) {
    work->badFile = true;
    copyBounded(work->badFileName, sizeof(work->badFileName), f.name, f.nameLen);
    return;
  }
  builder->addFile(f.name, f.nameLen, f.size, f.crc32, f.hasCrc32);
}

void FontManifestReader::familyEnd() {
  const Work& w = *work;
  if (w.familyNameOverflow || !familyNameOk(w.familyName)) {
    if (filling) {
      LOG_ERR(logTag, "Manifest entry rejected, invalid family name: %s", w.familyName);
    }
    builder->dropFamily();
    return;
  }
  if (w.badFile) {
    if (filling) {
      LOG_ERR(logTag, "Manifest entry rejected, invalid file name in %s: %s", w.familyName, w.badFileName);
    }
    builder->dropFamily();
    return;
  }
  builder->keepFamily(w.familyName, w.familyNameLen);
}
