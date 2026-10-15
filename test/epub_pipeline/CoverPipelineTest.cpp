// Two cover-pipeline costs the home screen was paying on every cold start, measured on X4 with a
// five-book carousel:
//
//   ~3.3 s  re-parsing content.opf. loadForCover() parses it whenever book.bin is absent, and the
//           SAME book is asked three to five times in a row (try the thumb, check cover.img, start
//           the extract, then once per thumb size). One Cyrillic book paid 5 x 301 ms.
//   ~6.0 s  copying covers to cover.img before any of them could be decoded -- pure byte-shuffling
//           whenever the ZIP stores the entry uncompressed.
//
// Both fixes are only sound if they change nothing observable, which is what these pin: the memo
// must never answer for the wrong book, and an in-place decode must produce the same thumbnail as
// one that went through cover.img.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Epub.h"
#include "Epub/CoverThumbSession.h"
#include "StoredZipWriter.h"

namespace fs = std::filesystem;

namespace {

// Minimal EPUB whose OPF declares `coverName` as the cover image. Without `declareCover` the image
// is still in the manifest but nothing names it the cover, and the file comes out the same size.
std::string makeEpub(const fs::path& path, const std::string& coverName, const std::string& coverBytes,
                     const std::string& title, const bool declareCover = true) {
  const std::string opf =
      "<?xml version=\"1.0\"?><package version=\"2.0\" xmlns=\"http://www.idpf.org/2007/opf\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>" +
      title + "</dc:title><meta name=\"" + (declareCover ? "cover" : "other") +
      "\" content=\"cov\"/></metadata>"
      "<manifest><item id=\"cov\" href=\"" +
      coverName +
      "\" media-type=\"image/png\"/>"
      "<item id=\"c1\" href=\"c1.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
      "<spine><itemref idref=\"c1\"/></spine></package>";
  test_zip::StoredZipWriter zip;
  zip.add("mimetype", "application/epub+zip");
  zip.add("META-INF/container.xml",
          "<?xml version=\"1.0\"?><container version=\"1.0\" "
          "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile "
          "full-path=\"content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
  zip.add("content.opf", opf);
  zip.add(coverName, coverBytes);
  zip.add("c1.xhtml", "<html><body><p>x</p></body></html>");
  zip.write(path.string());
  return path.string();
}

struct CoverPipelineFixture : testing::Test {
  fs::path work;
  std::string cacheDir;
  std::vector<uint8_t> pngBytes;

  void SetUp() override {
    work = fs::temp_directory_path() /
           (std::string("cover_pipe_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(work);
    fs::create_directories(work);
    cacheDir = (work / "cache").string();
    fs::create_directories(cacheDir);

    // A real PNG, pulled out of the corpus book (which deflates it). cache_test_1 rather than
    // scaling_test: the latter is 1200x1500, above the thumbnail path's MAX_PNG_PIXELS cap, so it
    // would fail here for a reason that has nothing to do with what is under test.
    const std::string extracted = (work / "src.png").string();
    Epub source(CORPUS_DIR "/test_png_images.epub", cacheDir);
    ASSERT_TRUE(source.extractItemToFile("OEBPS/images/cache_test_1.png", extracted, nullptr));
    std::ifstream in(extracted, std::ios::binary);
    pngBytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    ASSERT_GT(pngBytes.size(), 500u);
    Epub::clearCoverMetadataMemo();
  }
  void TearDown() override {
    Epub::clearCoverMetadataMemo();
    fs::remove_all(work);
  }

  std::string pngString() const { return std::string(pngBytes.begin(), pngBytes.end()); }
};

// The memo must not leak one book's cover into another's — the failure that would put the wrong
// picture on the home screen.
TEST_F(CoverPipelineFixture, MemoDoesNotAnswerForADifferentBook) {
  const std::string a = makeEpub(work / "a.epub", "coverA.png", pngString(), "A");
  const std::string b = makeEpub(work / "b.epub", "coverB.png", pngString() + "padding-to-differ", "B");

  Epub epubA(a, (work / "ca").string());
  ASSERT_TRUE(epubA.loadForCover());
  EXPECT_EQ(epubA.getCoverItemHref(), "coverA.png");

  Epub epubB(b, (work / "cb").string());
  ASSERT_TRUE(epubB.loadForCover());
  EXPECT_EQ(epubB.getCoverItemHref(), "coverB.png") << "the memo served the previous book";

  // And back again — a replaced entry must not have poisoned the first answer either.
  Epub epubA2(a, (work / "ca").string());
  ASSERT_TRUE(epubA2.loadForCover());
  EXPECT_EQ(epubA2.getCoverItemHref(), "coverA.png");
}

// Repeat queries for the same book are what the memo exists for. The one outside sign that a repeat
// was answered from it, rather than by parsing the OPF again, is a change the key cannot see: same
// path, same size, a different cover name.
TEST_F(CoverPipelineFixture, RepeatedLoadForCoverIsServedFromTheMemo) {
  const fs::path path = work / "a.epub";
  makeEpub(path, "coverA.png", pngString(), "A");
  const auto size = fs::file_size(path);
  {
    Epub epub(path.string(), (work / "ca").string());
    ASSERT_TRUE(epub.loadForCover());
    ASSERT_EQ(epub.getCoverItemHref(), "coverA.png");
  }
  makeEpub(path, "coverZ.png", pngString(), "A");
  ASSERT_EQ(fs::file_size(path), size) << "the rewrite must keep the memo key";
  for (int i = 0; i < 4; i++) {
    Epub epub(path.string(), (work / "ca").string());
    ASSERT_TRUE(epub.loadForCover()) << "attempt " << i;
    EXPECT_EQ(epub.getCoverItemHref(), "coverA.png") << "attempt " << i << " parsed the OPF again";
  }

  // The memo is what held the old answer: without it the same call sees the rewrite.
  Epub::clearCoverMetadataMemo();
  Epub epub(path.string(), (work / "ca").string());
  ASSERT_TRUE(epub.loadForCover());
  EXPECT_EQ(epub.getCoverItemHref(), "coverZ.png");
}

// Same path, different content: the size key must force a re-parse rather than serve the old cover.
TEST_F(CoverPipelineFixture, ReplacedBookAtTheSamePathIsNotServedStale) {
  const fs::path path = work / "book.epub";
  makeEpub(path, "coverA.png", pngString(), "A");
  {
    Epub epub(path.string(), (work / "c1").string());
    ASSERT_TRUE(epub.loadForCover());
    ASSERT_EQ(epub.getCoverItemHref(), "coverA.png");
  }
  fs::remove(path);
  makeEpub(path, "coverB.png", pngString() + "different-length", "B");
  {
    Epub epub(path.string(), (work / "c2").string());
    ASSERT_TRUE(epub.loadForCover());
    EXPECT_EQ(epub.getCoverItemHref(), "coverB.png") << "stale memo served after the book changed";
  }
}

// A book with no cover is memoized too — the "no cover" answer is exactly what the next three
// calls would otherwise re-parse the OPF to rediscover. Observed the same way: a same-size rewrite
// that does declare a cover goes unseen until the memo is cleared.
TEST_F(CoverPipelineFixture, BookWithoutACoverStaysWithoutOne) {
  const fs::path path = work / "nocover.epub";
  makeEpub(path, "pic.png", pngString(), "N", /*declareCover=*/false);
  const auto size = fs::file_size(path);
  {
    Epub epub(path.string(), (work / "cn").string());
    ASSERT_FALSE(epub.loadForCover());
  }
  makeEpub(path, "pic.png", pngString(), "N", /*declareCover=*/true);
  ASSERT_EQ(fs::file_size(path), size) << "the rewrite must keep the memo key";
  for (int i = 0; i < 3; i++) {
    Epub epub(path.string(), (work / "cn").string());
    EXPECT_FALSE(epub.loadForCover()) << "attempt " << i << " parsed the OPF again";
  }

  Epub::clearCoverMetadataMemo();
  Epub epub(path.string(), (work / "cn").string());
  ASSERT_TRUE(epub.loadForCover());
  EXPECT_EQ(epub.getCoverItemHref(), "pic.png");
}

// The in-place decode must produce exactly the thumbnail the extract-then-decode path does.
// The memo also answers "is the cover stored, and where" for the book it holds. Replacing the
// book in the memo kept that answer, so the next book was decoded at the previous book's cover
// offset -- in its own file -- rejected as an unsupported image, and recorded as having no cover.
// Seen on device: two books in a row in the Covers view, the second one's "47365 bytes at 110"
// copied from the first.
TEST_F(CoverPipelineFixture, StoredCoverLocationIsNotCarriedToTheNextBook) {
  // Different title lengths put the stored cover entry at a different offset in each archive.
  const std::string a = makeEpub(work / "a.epub", "cover.png", pngString(), "A");
  const std::string b =
      makeEpub(work / "b.epub", "cover.png", pngString(), "B, with a title long enough to move the cover entry");

  std::vector<uint8_t> reference;
  {
    Epub epub(b, (work / "ref").string());
    ASSERT_TRUE(epub.loadForCover());
    ASSERT_EQ(epub.generateThumbBmp(120, 160, /*allowExtract=*/false), ThumbResult::Ok);
    std::ifstream in(epub.getThumbBmpPath(120, 160), std::ios::binary);
    reference.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }
  ASSERT_FALSE(reference.empty());
  Epub::clearCoverMetadataMemo();

  {
    Epub epubA(a, (work / "ca").string());
    ASSERT_TRUE(epubA.loadForCover());
    ASSERT_EQ(epubA.generateThumbBmp(120, 160, /*allowExtract=*/false), ThumbResult::Ok);
  }
  Epub epubB(b, (work / "cb").string());
  ASSERT_TRUE(epubB.loadForCover());
  ASSERT_EQ(epubB.generateThumbBmp(120, 160, /*allowExtract=*/false), ThumbResult::Ok)
      << "the second book was decoded at the first book's cover offset";
  std::ifstream in(epubB.getThumbBmpPath(120, 160), std::ios::binary);
  const std::vector<uint8_t> thumb{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  EXPECT_EQ(thumb, reference);
}

// A book that has no cover is a structural absence -- record it once and stop asking -- but a book
// whose OPF could not be read is not: that may be a tight heap, and deserves another try.
TEST_F(CoverPipelineFixture, NoCoverIsKnownAbsentButAnUnreadableBookIsNot) {
  const std::string coverless = makeEpub(work / "n.epub", "pic.png", pngString(), "N", /*declareCover=*/false);
  for (int attempt = 0; attempt < 2; ++attempt) {  // the parse, then the memo
    Epub epub(coverless, (work / "cn").string());
    ASSERT_FALSE(epub.loadForCover());
    EXPECT_TRUE(epub.coverKnownAbsent()) << "attempt " << attempt;
  }

  // A zip with no content.opf at all: nothing was learnt about the cover.
  test_zip::StoredZipWriter zip;
  zip.add("mimetype", "application/epub+zip");
  zip.add("c1.xhtml", "<html><body><p>x</p></body></html>");
  const std::string broken = (work / "broken.epub").string();
  zip.write(broken);
  Epub epub(broken, (work / "cbroken").string());
  ASSERT_FALSE(epub.loadForCover());
  EXPECT_FALSE(epub.coverKnownAbsent());
}

TEST_F(CoverPipelineFixture, StoredCoverThumbMatchesTheExtractedPath) {
  const std::string book = makeEpub(work / "book.epub", "cover.png", pngString(), "T");

  const auto read = [](const std::string& p) {
    std::ifstream in(p, std::ios::binary);
    return std::vector<uint8_t>{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  };

  // Route 1: nothing cached -> the stored entry is decoded straight out of the archive.
  std::vector<uint8_t> viaInPlace;
  std::string inPlaceCoverImg;
  {
    Epub epub(book, (work / "inplace").string());
    ASSERT_TRUE(epub.loadForCover());
    inPlaceCoverImg = epub.getCoverImageCachePath();
    ASSERT_EQ(epub.generateThumbBmp(120, 160, /*allowExtract=*/false), ThumbResult::Ok)
        << "a stored cover should need no extraction at all";
    viaInPlace = read(epub.getThumbBmpPath(120, 160));
  }
  ASSERT_FALSE(viaInPlace.empty());
  EXPECT_FALSE(fs::exists(inPlaceCoverImg)) << "the whole point: no copy of the cover on the card";

  // Route 2: hand it a cover.img first, so the ordinary cached path runs instead.
  Epub::clearCoverMetadataMemo();
  {
    Epub epub(book, (work / "extracted").string());
    ASSERT_TRUE(epub.loadForCover());
    const fs::path img = epub.getCoverImageCachePath();
    fs::create_directories(img.parent_path());
    {
      std::ofstream out(img.string(), std::ios::binary);
      out.write(reinterpret_cast<const char*>(pngBytes.data()), static_cast<std::streamsize>(pngBytes.size()));
    }
    ASSERT_EQ(epub.generateThumbBmp(120, 160, /*allowExtract=*/false), ThumbResult::Ok);
    EXPECT_EQ(read(epub.getThumbBmpPath(120, 160)), viaInPlace)
        << "decoding in place must be indistinguishable from decoding the extracted copy";
  }
}

// R9 item 2 (memory audit 2026-09): the Lyra carousel's two thumbnail sizes come from ONE decode of
// the cover. Both routes a cover can take -- an extracted cover.img, and a JPEG stored uncompressed
// and decoded in place out of the archive -- must write both BMPs complete, and the larger must be
// byte-identical to generating that size alone (same decode scale, resampling and dither).
std::string readFileString(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

TEST_F(CoverPipelineFixture, BothCarouselThumbsFromOneJpegDecode) {
  const std::string jpeg = readFileString(JPEG_FIXTURE_DIR "/prog_full_420.jpg");
  ASSERT_GT(jpeg.size(), 1000u);
  const std::pair<int, int> sizes[2] = {{90, 60}, {40, 28}};

  // Reference: the larger size generated on its own, from an extracted cover.img.
  const std::string refBook = makeEpub(work / "ref.epub", "cover.jpg", jpeg, "R");
  std::string reference;
  {
    Epub epub(refBook, (work / "ref").string());
    ASSERT_TRUE(epub.loadForCover());
    const fs::path img = epub.getCoverImageCachePath();
    fs::create_directories(img.parent_path());
    std::ofstream(img.string(), std::ios::binary) << jpeg;
    ASSERT_EQ(epub.generateThumbBmp(90, 60, /*allowExtract=*/false), ThumbResult::Ok);
    reference = readFileString(epub.getThumbBmpPath(90, 60));
  }
  ASSERT_FALSE(reference.empty());

  for (const bool inPlace : {false, true}) {
    SCOPED_TRACE(inPlace ? "stored entry decoded in place" : "extracted cover.img");
    Epub::clearCoverMetadataMemo();
    const std::string book = makeEpub(work / (inPlace ? "inplace.epub" : "cached.epub"), "cover.jpg", jpeg, "T");
    Epub epub(book, (work / (inPlace ? "inplace" : "cached")).string());
    ASSERT_TRUE(epub.loadForCover());
    if (!inPlace) {
      const fs::path img = epub.getCoverImageCachePath();
      fs::create_directories(img.parent_path());
      std::ofstream(img.string(), std::ios::binary) << jpeg;
    }
    ASSERT_EQ(epub.generateThumbBmps(sizes, 2, /*allowExtract=*/false), ThumbResult::Ok);
    EXPECT_EQ(readFileString(epub.getThumbBmpPath(90, 60)), reference);
    const std::string small = readFileString(epub.getThumbBmpPath(40, 28));
    ASSERT_GE(small.size(), 62u);
    // 40x28 1-bit, top-down: 62-byte header + 28 rows of 8 bytes.
    EXPECT_EQ(small.size(), 62u + 28u * 8u);
    // A second call finds both complete and decodes nothing: with the cached cover replaced by
    // bytes no decoder takes, a decode would fail.
    if (!inPlace) {
      std::ofstream(epub.getCoverImageCachePath(), std::ios::binary | std::ios::trunc) << "\xFF\xD8\xFF\xE0garbage";
    }
    EXPECT_EQ(epub.generateThumbBmps(sizes, 2, /*allowExtract=*/false), ThumbResult::Ok);
    EXPECT_EQ(readFileString(epub.getThumbBmpPath(90, 60)), reference);
  }
}

// R9 item 3: the same two thumbnails, converted in slices the way Home's cover pass drives them.
// Both routes must write exactly what the one-shot generateThumbBmps writes; a session abandoned
// mid-decode (Home left for a book) must leave no partial thumbnail behind; and a cover the
// one-shot path owns (a sentinel, both sizes already complete) must not start a session.
TEST_F(CoverPipelineFixture, SlicedCarouselThumbsMatchTheOneShotDecode) {
  const std::pair<int, int> sizes[2] = {{90, 60}, {40, 28}};
  for (const char* fixture : {"prog_full_420.jpg", "prog_full_420_base.jpg"}) {
    const std::string jpeg = readFileString(std::string(JPEG_FIXTURE_DIR "/") + fixture);
    ASSERT_GT(jpeg.size(), 1000u);
    for (const bool inPlace : {false, true}) {
      SCOPED_TRACE(std::string(fixture) + (inPlace ? ", stored entry decoded in place" : ", extracted cover.img"));
      const std::string tag = std::string(inPlace ? "ip_" : "cc_") + fixture;
      const std::string book = makeEpub(work / (tag + ".epub"), "cover.jpg", jpeg, "T");
      const auto prime = [&](Epub& epub) {
        ASSERT_TRUE(epub.loadForCover());
        if (!inPlace) {
          const fs::path img = epub.getCoverImageCachePath();
          fs::create_directories(img.parent_path());
          std::ofstream(img.string(), std::ios::binary) << jpeg;
        }
      };

      // Reference: the one-shot conversion.
      Epub::clearCoverMetadataMemo();
      std::string refLarge, refSmall;
      {
        Epub epub(book, (work / (tag + "_ref")).string());
        prime(epub);
        ASSERT_EQ(epub.generateThumbBmps(sizes, 2, /*allowExtract=*/false), ThumbResult::Ok);
        refLarge = readFileString(epub.getThumbBmpPath(90, 60));
        refSmall = readFileString(epub.getThumbBmpPath(40, 28));
      }

      Epub::clearCoverMetadataMemo();
      Epub epub(book, (work / (tag + "_sliced")).string());
      prime(epub);

      // Abandoned halfway: nothing left behind, not even a 0-byte file (that would read as "no cover").
      {
        auto session = epub.beginThumbSession(sizes, 2);
        ASSERT_NE(session, nullptr);
        EXPECT_EQ(session->outputCount(), 2);
        for (int i = 0; i < 3; ++i) ASSERT_EQ(session->continueSteps(1), CoverThumbSession::Status::Running);
      }
      EXPECT_FALSE(fs::exists(epub.getThumbBmpPath(90, 60)));
      EXPECT_FALSE(fs::exists(epub.getThumbBmpPath(40, 28)));

      // Driven to the end, one unit at a time.
      {
        auto session = epub.beginThumbSession(sizes, 2);
        ASSERT_NE(session, nullptr);
        auto status = CoverThumbSession::Status::Running;
        int units = 0;
        while (status == CoverThumbSession::Status::Running && units < 100000) {
          status = session->continueSteps(1);
          ++units;
        }
        EXPECT_EQ(status, CoverThumbSession::Status::Done);
        EXPECT_GT(units, 4);
      }
      EXPECT_EQ(readFileString(epub.getThumbBmpPath(90, 60)), refLarge);
      EXPECT_EQ(readFileString(epub.getThumbBmpPath(40, 28)), refSmall);

      // Both complete: nothing to start. A sentinel on either: left to the one-shot path.
      EXPECT_EQ(epub.beginThumbSession(sizes, 2), nullptr);
      fs::remove(epub.getThumbBmpPath(40, 28));
      std::ofstream(epub.getThumbBmpPath(40, 28), std::ios::binary).close();
      EXPECT_EQ(epub.beginThumbSession(sizes, 2), nullptr);
    }
  }
}

// A PNG cover keeps its own sliced path (PngDecodeSession); the JPEG session does not take it.
TEST_F(CoverPipelineFixture, SlicedCarouselThumbsLeavePngCoversAlone) {
  const std::string book = makeEpub(work / "png.epub", "cover.png", pngString(), "P");
  Epub epub(book, (work / "png").string());
  ASSERT_TRUE(epub.loadForCover());
  const std::pair<int, int> sizes[2] = {{90, 60}, {40, 28}};
  EXPECT_EQ(epub.beginThumbSession(sizes, 2), nullptr);
  EXPECT_FALSE(fs::exists(epub.getThumbBmpPath(90, 60)));
}

// A cover the OPF does not declare leaves a structural sentinel for every size at once.
TEST_F(CoverPipelineFixture, BothCarouselThumbsGetSentinelsWhenTheCoverIsAbsent) {
  const std::string opf =
      "<?xml version=\"1.0\"?><package version=\"2.0\" xmlns=\"http://www.idpf.org/2007/opf\">"
      "<metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>N</dc:title></metadata>"
      "<manifest><item id=\"c1\" href=\"c1.xhtml\" media-type=\"application/xhtml+xml\"/></manifest>"
      "<spine><itemref idref=\"c1\"/></spine></package>";
  test_zip::StoredZipWriter zip;
  zip.add("mimetype", "application/epub+zip");
  zip.add("META-INF/container.xml",
          "<?xml version=\"1.0\"?><container version=\"1.0\" "
          "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile "
          "full-path=\"content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
  zip.add("content.opf", opf);
  zip.add("c1.xhtml", "<html><body><p>x</p></body></html>");
  const std::string book = (work / "nocover.epub").string();
  zip.write(book);

  // loadForCover() refuses a book without a cover item (the caller then counts a transient, as
  // for one size); a full load is what reaches generateThumbBmps' own structural answer.
  Epub epub(book, (work / "nc").string());
  ASSERT_TRUE(epub.load(true));
  const std::pair<int, int> sizes[2] = {{90, 60}, {40, 28}};
  EXPECT_EQ(epub.generateThumbBmps(sizes, 2, /*allowExtract=*/false), ThumbResult::StructurallyAbsent);
  for (const auto& sz : sizes) {
    ASSERT_TRUE(fs::exists(epub.getThumbBmpPath(sz.first, sz.second)));
    EXPECT_EQ(fs::file_size(epub.getThumbBmpPath(sz.first, sz.second)), 0u) << "0-byte sentinel";
  }
}

}  // namespace
