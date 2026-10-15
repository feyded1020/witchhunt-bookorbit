// A TOC lost by an index build is retried on the reader's next open.
//
// book.bin is reused as it is, so a TOC that a first open lost to a passing shortage (the NCX
// parser or its inflate ring refused on a fragmented heap, seen on an X3) stayed lost for good:
// the book read without chapters until its cache was cleared by hand. The build now records the
// loss in toc.retry and the reader's open rebuilds, at most three builds in all.
//
// The device's failure is a heap state the host cannot reproduce on cue, so a book whose NCX does
// not parse stands in for it. To recover, the same book with a parsing NCX takes its place, and
// fingerprint.bin is removed so the cache is adopted rather than wiped as a different book: the
// spine is identical, only the TOC now parses -- what a retry on a roomier heap sees.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "Epub.h"
#include "StoredZipWriter.h"

namespace fs = std::filesystem;

namespace {

constexpr int kSpines = 6;

enum class Toc { Parses, Broken, None };

class LostTocRetry : public testing::Test {
 protected:
  fs::path work;
  std::string bookPath;
  std::string cacheDir;

  void SetUp() override {
    work = fs::temp_directory_path() /
           (std::string("lost_toc_retry_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(work);
    fs::create_directories(work);
    bookPath = (work / "book.epub").string();
    cacheDir = (work / "cache").string();
  }
  void TearDown() override { fs::remove_all(work); }

  void writeBook(const Toc toc) const {
    test_zip::StoredZipWriter zip;
    zip.add("mimetype", "application/epub+zip");
    zip.add("META-INF/container.xml",
            "<?xml version=\"1.0\"?>\n<container version=\"1.0\" "
            "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\">\n<rootfiles><rootfile "
            "full-path=\"content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles>\n</container>\n");
    std::string manifest;
    std::string spine;
    std::string navPoints;
    for (int i = 0; i < kSpines; ++i) {
      const std::string id = "c" + std::to_string(i);
      manifest += "<item id=\"" + id + "\" href=\"" + id + ".xhtml\" media-type=\"application/xhtml+xml\"/>\n";
      spine += "<itemref idref=\"" + id + "\"/>\n";
      navPoints += "<navPoint id=\"n" + std::to_string(i) + "\"><navLabel><text>Chapter " + std::to_string(i) +
                   "</text></navLabel><content src=\"" + id + ".xhtml\"/></navPoint>\n";
      zip.add(id + ".xhtml",
              "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<html xmlns=\"http://www.w3.org/1999/xhtml\">"
              "<head><title>C</title><link rel=\"stylesheet\" href=\"style.css\"/></head><body><p>Chapter " +
                  std::to_string(i) + " text.</p></body></html>\n");
    }
    // A stylesheet, so a reader open leaves a compiled CSS cache and needsFirstOpenIndexing() answers
    // for the TOC alone.
    manifest += "<item id=\"css\" href=\"style.css\" media-type=\"text/css\"/>\n";
    zip.add("style.css", "p { text-indent: 1em; }\n");
    if (toc != Toc::None) {
      manifest += "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>\n";
      const std::string ncxHead =
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" "
          "version=\"2005-1\">\n<navMap>\n";
      // Broken: the navMap is never closed, so the parser stops at </ncx> before any entry is made.
      zip.add("toc.ncx", toc == Toc::Parses ? ncxHead + navPoints + "</navMap>\n</ncx>\n" : ncxHead + "</ncx>\n");
    }
    zip.add("content.opf",
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package xmlns=\"http://www.idpf.org/2007/opf\" "
            "version=\"2.0\" unique-identifier=\"id\">\n<metadata "
            "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"id\">toc-retry</dc:identifier>"
            "<dc:title>Toc Retry</dc:title><dc:language>en</dc:language></metadata>\n<manifest>\n" +
                manifest + "</manifest>\n<spine" + (toc != Toc::None ? " toc=\"ncx\"" : "") + ">\n" + spine +
                "</spine>\n</package>\n");
    fs::remove(bookPath);
    zip.write(bookPath);
  }

  // As ReaderActivity::loadEpub opens a book: the one load allowed to retry a lost TOC.
  std::unique_ptr<Epub> openInReader() const {
    auto epub = std::make_unique<Epub>(bookPath, cacheDir);
    epub->setLostTocRetryEnabled(true);
    EXPECT_TRUE(epub->load(true));
    return epub;
  }

  // As the sleep screen, book info and KOReader sync load a book: for its metadata.
  std::unique_ptr<Epub> openForMetadata() const {
    auto epub = std::make_unique<Epub>(bookPath, cacheDir);
    EXPECT_TRUE(epub->load(true, true));
    return epub;
  }

  bool needsIndexing() const { return Epub(bookPath, cacheDir).needsFirstOpenIndexing(); }

  std::string cachePath() const { return Epub(bookPath, cacheDir).getCachePath(); }

  int tocLosses() const {
    std::ifstream f(cachePath() + "/toc.retry", std::ios::binary);
    if (!f) return 0;
    char count = 0;
    f.read(&count, 1);
    return static_cast<unsigned char>(count);
  }

  // Stand-ins for what reading the book without its TOC left behind. Any rebuild that recompiles
  // the CSS clears the section caches; only a recovered TOC also drops pages.bin.
  void plantReadingLeftovers() const {
    fs::create_directories(Epub::spineCacheRoot(cachePath()) + "/0");
    std::ofstream(Epub::spineCacheRoot(cachePath()) + "/0/canary") << "x";
    std::ofstream(cachePath() + "/pages.bin") << "x";
  }
  bool sectionsKept() const { return fs::exists(Epub::spineCacheRoot(cachePath()) + "/0/canary"); }
  bool pageCountsKept() const { return fs::exists(cachePath() + "/pages.bin"); }

  // The same book, its TOC now parsing; adopted by the next open rather than wiped as another book.
  void tocParsesFromNowOn() const {
    writeBook(Toc::Parses);
    fs::remove(cachePath() + "/fingerprint.bin");
  }
};

}  // namespace

TEST_F(LostTocRetry, ABuildThatLosesTheTocRecordsItForTheReadersNextOpen) {
  writeBook(Toc::Broken);
  const auto epub = openInReader();

  EXPECT_EQ(epub->getSpineItemsCount(), kSpines);
  EXPECT_EQ(epub->getTocItemsCount(), 0);
  EXPECT_EQ(tocLosses(), 1);
  EXPECT_TRUE(needsIndexing()) << "the reader must get its popup and lend the framebuffer for the retry";
}

TEST_F(LostTocRetry, TheReadersNextOpenRecoversTheToc) {
  writeBook(Toc::Broken);
  openInReader();
  plantReadingLeftovers();
  tocParsesFromNowOn();

  const auto epub = openInReader();
  EXPECT_EQ(epub->getTocItemsCount(), kSpines);
  EXPECT_EQ(epub->getSpineIndexForTocIndex(kSpines - 1), kSpines - 1);
  EXPECT_EQ(tocLosses(), 0) << "a recovered TOC must not be retried again";
  EXPECT_FALSE(needsIndexing());
  // The TOC's chapter starts change the layout the book was read in without it.
  EXPECT_FALSE(sectionsKept());
  EXPECT_FALSE(pageCountsKept());

  // And stays: the next open reads the cache it rebuilt.
  plantReadingLeftovers();
  EXPECT_EQ(openInReader()->getTocItemsCount(), kSpines);
  EXPECT_TRUE(sectionsKept());
  EXPECT_TRUE(pageCountsKept());
}

TEST_F(LostTocRetry, OnlyTheReadersOpenRetries) {
  writeBook(Toc::Broken);
  openInReader();
  plantReadingLeftovers();
  tocParsesFromNowOn();

  // The sleep screen and book info load the book too, with no popup and no lent framebuffer.
  EXPECT_EQ(openForMetadata()->getTocItemsCount(), 0);
  EXPECT_EQ(tocLosses(), 1);
  EXPECT_TRUE(sectionsKept());
  EXPECT_TRUE(pageCountsKept());
}

TEST_F(LostTocRetry, ATocThatNeverParsesStopsCostingARebuild) {
  writeBook(Toc::Broken);
  openInReader();  // the first build
  openInReader();  // retry 1
  EXPECT_EQ(tocLosses(), 2);
  EXPECT_TRUE(needsIndexing());
  openInReader();  // retry 2, the last
  EXPECT_EQ(tocLosses(), 3);
  EXPECT_FALSE(needsIndexing());

  plantReadingLeftovers();
  EXPECT_EQ(openInReader()->getTocItemsCount(), 0);
  EXPECT_EQ(tocLosses(), 3);
  EXPECT_TRUE(sectionsKept()) << "no fourth build";
}

TEST_F(LostTocRetry, ABookWithoutATocHasNothingToRetry) {
  writeBook(Toc::None);
  EXPECT_EQ(openInReader()->getTocItemsCount(), 0);
  EXPECT_EQ(tocLosses(), 0);
  EXPECT_FALSE(needsIndexing());
}
