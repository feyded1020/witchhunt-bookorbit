// The single definition of what counts as a sidecar (lib/FsHelpers/SidecarFiles).
//
// This logic used to be copied into three places - the cover resolver, the
// metadata resolver, and the move-to-/COMPLETED extension list - and the copies
// had drifted: the move path derived its base name with rfind('.') alone, with
// no separator check, so a book with no extension inside a dotted folder took
// the dot from the folder. Centralising it made that reachable from a test,
// which is most of the point.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
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

  void touch(const std::string& name) {
    fs::create_directories(fs::path(work / name).parent_path());
    std::ofstream(work / name, std::ios::binary) << "x";
  }
  std::string p(const std::string& name) const { return (work / name).string(); }
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
  EXPECT_TRUE(SidecarFiles::existingPaths(p("book.epub")).empty());
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
TEST_F(SidecarFilesFixture, ExistingPathsCoversEveryKind) {
  touch("book.epub");
  touch("book.jpg");
  touch("book.opf");
  touch("book.epub.rights");
  const auto found = SidecarFiles::existingPaths(p("book.epub"));
  ASSERT_EQ(found.size(), 3u) << "cover, metadata and rights document are all sidecars";
  EXPECT_NE(std::find(found.begin(), found.end(), p("book.jpg")), found.end());
  EXPECT_NE(std::find(found.begin(), found.end(), p("book.opf")), found.end());
  EXPECT_NE(std::find(found.begin(), found.end(), p("book.epub.rights")), found.end())
      << "a rights document hangs off the FULL name, not the base name";
}

TEST_F(SidecarFilesFixture, ExistingPathsIgnoresUnrelatedNeighbours) {
  touch("book.epub");
  touch("book.opf");
  touch("bookmark.jpg");          // shares a prefix, not the base name
  touch("book.epub.rights.bak");  // shares a prefix with the rights document
  touch("other.png");
  const auto found = SidecarFiles::existingPaths(p("book.epub"));
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0], p("book.opf"));
}

// The tables list every extension in both cases, and the SD card is FAT/exFAT,
// which answers to either. One file must therefore be reported once - a caller
// that moves them would otherwise rename it twice, the second failing because
// the first already moved it. (On a case-sensitive host this passes trivially;
// it is the case-insensitive platforms, including the device, that need it.)
TEST_F(SidecarFilesFixture, OneFileIsReportedOncePerExtension) {
  touch("book.epub");
  touch("book.jpg");
  const auto found = SidecarFiles::existingPaths(p("book.epub"));
  ASSERT_EQ(found.size(), 1u) << "a single cover reported under both .jpg and .JPG";
  EXPECT_EQ(found[0], p("book.jpg"));
}

// A book with no extension of its own has no base name to hang a cover off - but a rights
// document appends to the full name, so it is still found. The two rules genuinely differ.
TEST_F(SidecarFilesFixture, SuffixSidecarsWorkWithoutAnExtension) {
  touch("untitled");
  touch("untitled.rights");
  const auto found = SidecarFiles::existingPaths(p("untitled"));
  ASSERT_EQ(found.size(), 1u);
  EXPECT_EQ(found[0], p("untitled.rights"));
}

// What every mover needs: each sidecar rewritten by its OWN rule. Getting this wrong would
// send "book.epub.rights" to "done.rights" and leave a protected book unable to open.
TEST_F(SidecarFilesFixture, MovePairsRewriteEachSidecarByItsOwnRule) {
  touch("book.epub");
  touch("book.jpg");
  touch("book.opf");
  touch("book.epub.rights");

  const auto pairs = SidecarFiles::movePairs(p("book.epub"), p("COMPLETED/book.epub"));
  ASSERT_EQ(pairs.size(), 3u);

  std::map<std::string, std::string> bySource(pairs.begin(), pairs.end());
  EXPECT_EQ(bySource[p("book.jpg")], p("COMPLETED/book.jpg"));
  EXPECT_EQ(bySource[p("book.opf")], p("COMPLETED/book.opf"));
  EXPECT_EQ(bySource[p("book.epub.rights")], p("COMPLETED/book.epub.rights"));
}

// A rename changes the name, not just the folder - the same two rules still apply.
TEST_F(SidecarFilesFixture, MovePairsHandleARename) {
  touch("book.epub");
  touch("book.jpg");
  touch("book.epub.rights");

  const auto pairs = SidecarFiles::movePairs(p("book.epub"), p("renamed.epub"));
  ASSERT_EQ(pairs.size(), 2u);
  std::map<std::string, std::string> bySource(pairs.begin(), pairs.end());
  EXPECT_EQ(bySource[p("book.jpg")], p("renamed.jpg"));
  EXPECT_EQ(bySource[p("book.epub.rights")], p("renamed.epub.rights"));
}

// Deleting the book takes its sidecars, and nothing else.
TEST_F(SidecarFilesFixture, RemoveAllTakesEverySidecarAndNothingElse) {
  touch("book.epub");
  touch("book.jpg");
  touch("book.epub.rights");
  touch("bookmark.jpg");

  EXPECT_EQ(SidecarFiles::removeAll(p("book.epub")), 2u);
  EXPECT_FALSE(fs::exists(work / "book.jpg"));
  EXPECT_FALSE(fs::exists(work / "book.epub.rights"));
  EXPECT_TRUE(fs::exists(work / "book.epub")) << "removeAll takes the sidecars, not the book";
  EXPECT_TRUE(fs::exists(work / "bookmark.jpg"));
}

TEST_F(SidecarFilesFixture, ExtensionlessBookHasNoSidecars) {
  touch("untitled");
  touch("untitled.opf");
  EXPECT_EQ(SidecarFiles::metadataPath(p("untitled")), "");
  EXPECT_TRUE(SidecarFiles::existingPaths(p("untitled")).empty());
}

}  // namespace
