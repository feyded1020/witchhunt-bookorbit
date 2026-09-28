// The single definition of what counts as a sidecar (lib/FsHelpers/SidecarFiles).
//
// This logic used to be copied into three places - the cover resolver, the
// metadata resolver, and the move-to-/COMPLETED extension list - and the copies
// had drifted: the move path derived its base name with rfind('.') alone, with
// no separator check, so a book with no extension inside a dotted folder took
// the dot from the folder. Centralising it made that reachable from a test,
// which is most of the point.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "SidecarFiles.h"

namespace fs = std::filesystem;

namespace {

struct SidecarFilesFixture : testing::Test {
  fs::path work;

  void SetUp() override {
    work = fs::temp_directory_path() /
           (std::string("sidecar_files_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(work);
    fs::create_directories(work);
  }
  void TearDown() override { fs::remove_all(work); }

  void touch(const std::string& name) { write(name, "x"); }
  void write(const std::string& name, const std::string& contents) {
    fs::create_directories(fs::path(work / name).parent_path());
    std::ofstream(work / name, std::ios::binary | std::ios::trunc) << contents;
  }
  std::string p(const std::string& name) const { return (work / name).string(); }
  // A second name for the same file, as a case-insensitive filesystem answers to. A host that is
  // case-insensitive itself already does, and would refuse the link.
  void alias(const std::string& name, const std::string& otherCase) {
    if (!fs::exists(work / otherCase)) fs::create_hard_link(work / name, work / otherCase);
  }
};

TEST_F(SidecarFilesFixture, BasePathStripsTheExtension) {
  EXPECT_EQ(SidecarFiles::basePath("/Books/Some Book.epub"), "/Books/Some Book");
  EXPECT_EQ(SidecarFiles::basePath("/Books/Dotted.Name.epub"), "/Books/Dotted.Name");
}

TEST_F(SidecarFilesFixture, BasePathRejectsPathsWithoutTheirOwnExtension) {
  EXPECT_EQ(SidecarFiles::basePath("/Books/untitled"), "");
  EXPECT_EQ(SidecarFiles::basePath("untitled"), "");
  // The regression the old move-to-/COMPLETED copy had: the only dot belongs to
  // the folder, so there is no extension to swap and no sidecar to find.
  EXPECT_EQ(SidecarFiles::basePath("/My.Books/untitled"), "");
  EXPECT_EQ(SidecarFiles::basePath("/My.Books/real.epub"), "/My.Books/real");
}

TEST_F(SidecarFilesFixture, NoSidecarsFound) {
  touch("book.epub");
  EXPECT_EQ(SidecarFiles::coverPath(p("book.epub")), "");
  EXPECT_EQ(SidecarFiles::metadataPath(p("book.epub")), "");
  EXPECT_TRUE(SidecarFiles::existingExtensions(p("book.epub")).empty());
}

TEST_F(SidecarFilesFixture, ResolvesCoverAndMetadataIndependently) {
  touch("book.epub");
  touch("book.png");
  touch("book.opf");
  EXPECT_EQ(SidecarFiles::coverPath(p("book.epub")), p("book.png"));
  EXPECT_EQ(SidecarFiles::metadataPath(p("book.epub")), p("book.opf"));
}

// Declared order decides the winner, so a book carrying several images resolves
// predictably rather than by directory-iteration luck.
TEST_F(SidecarFilesFixture, CoverResolutionFollowsDeclaredOrder) {
  touch("book.epub");
  touch("book.bmp");
  touch("book.jpg");
  EXPECT_EQ(SidecarFiles::coverPath(p("book.epub")), p("book.jpg")) << ".jpg is declared before .bmp";
}

// What the /COMPLETED move iterates: it has to see covers and metadata alike,
// or a finished book strands whichever kind it missed.
TEST_F(SidecarFilesFixture, ExistingExtensionsCoversBothKinds) {
  touch("book.epub");
  touch("book.jpg");
  touch("book.opf");
  const auto found = SidecarFiles::existingExtensions(p("book.epub"));
  ASSERT_EQ(found.size(), 2u);
  EXPECT_STREQ(found[0], ".jpg");
  EXPECT_STREQ(found[1], ".opf") << "metadata sidecar must travel with the book too";
}

TEST_F(SidecarFilesFixture, ExistingExtensionsIgnoresUnrelatedNeighbours) {
  touch("book.epub");
  touch("book.opf");
  touch("bookmark.jpg");  // shares a prefix, not the base name
  touch("other.png");
  const auto found = SidecarFiles::existingExtensions(p("book.epub"));
  ASSERT_EQ(found.size(), 1u);
  EXPECT_STREQ(found[0], ".opf");
}

// The tables list every extension in both cases, and the SD card is FAT/exFAT,
// which answers to either. One file must therefore be reported once - a caller
// that moves them would otherwise rename it twice, the second failing because
// the first already moved it. The host filesystem is case-sensitive, so a hard
// link gives each file its upper-case name the way FAT would; without it this
// passes whether or not anything is deduplicated.
TEST_F(SidecarFilesFixture, OneFileIsReportedOncePerExtension) {
  touch("book.epub");
  touch("book.jpg");
  touch("book.opf");
  alias("book.jpg", "book.JPG");
  alias("book.opf", "book.OPF");
  const auto found = SidecarFiles::existingExtensions(p("book.epub"));
  ASSERT_EQ(found.size(), 2u) << "a single file reported under both cases of its extension";
  EXPECT_STREQ(found[0], ".jpg");
  EXPECT_STREQ(found[1], ".opf");
}

TEST_F(SidecarFilesFixture, ExtensionlessBookHasNoSidecars) {
  touch("untitled");
  touch("untitled.opf");
  EXPECT_EQ(SidecarFiles::metadataPath(p("untitled")), "");
  EXPECT_TRUE(SidecarFiles::existingExtensions(p("untitled")).empty());
}

// metadataStamp() is how the recent-books list notices a sidecar written after
// the book was opened. It must tell "none" from "present", and see any edit.
TEST_F(SidecarFilesFixture, MetadataStampIsZeroWithoutASidecar) {
  touch("book.epub");
  EXPECT_EQ(SidecarFiles::metadataStamp(p("book.epub")), 0u);
}

TEST_F(SidecarFilesFixture, MetadataStampIsNonZeroEvenForAnEmptySidecar) {
  touch("book.epub");
  write("book.opf", "");
  EXPECT_NE(SidecarFiles::metadataStamp(p("book.epub")), 0u);
}

TEST_F(SidecarFilesFixture, MetadataStampIsStableForUnchangedContent) {
  touch("book.epub");
  write("book.opf", "<dc:title>Alpha</dc:title>");
  const uint32_t first = SidecarFiles::metadataStamp(p("book.epub"));
  write("book.opf", "<dc:title>Alpha</dc:title>");
  EXPECT_EQ(SidecarFiles::metadataStamp(p("book.epub")), first);
}

// The case a size or clock-based stamp misses: a device without a set clock
// dates every write 1980-01-01, and this edit keeps the length.
TEST_F(SidecarFilesFixture, MetadataStampSeesASameLengthEdit) {
  touch("book.epub");
  write("book.opf", "<dc:title>Alpha</dc:title>");
  const uint32_t before = SidecarFiles::metadataStamp(p("book.epub"));
  write("book.opf", "<dc:title>Omega</dc:title>");
  EXPECT_NE(SidecarFiles::metadataStamp(p("book.epub")), before);
}

// The last byte a reader of the sidecar can take in is still hashed.
TEST_F(SidecarFilesFixture, MetadataStampCoversTheWholeReadableSidecar) {
  touch("book.epub");
  std::string contents(SidecarFiles::kMetadataStampBytes, ' ');
  write("book.opf", contents);
  const uint32_t before = SidecarFiles::metadataStamp(p("book.epub"));
  contents.back() = 'x';
  write("book.opf", contents);
  EXPECT_NE(SidecarFiles::metadataStamp(p("book.epub")), before);
}

}  // namespace
