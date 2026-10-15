// A TOC parse that fails partway must not leave its printed-page list behind, and must drop it
// through the sink that holds it open: deleting pagelist.bin from outside left the sink to patch
// its count into, and close, a file that no longer existed.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "Epub/parsers/PageListSink.h"

namespace fs = std::filesystem;

namespace {

class PageListSinkTest : public testing::Test {
 protected:
  fs::path dir;

  void SetUp() override {
    dir = fs::temp_directory_path() /
          (std::string("page_list_sink_") + testing::UnitTest::GetInstance()->current_test_info()->name());
    fs::remove_all(dir);
    fs::create_directories(dir);
  }
  void TearDown() override { fs::remove_all(dir); }

  bool listExists() const { return fs::exists(dir / "pagelist.bin"); }
};

TEST_F(PageListSinkTest, DiscardDropsAListWithEntries) {
  PageListSink sink(dir.string());
  ASSERT_TRUE(sink.isOpen());
  sink.addEntry("c1.xhtml", "p1", "1");
  sink.addEntry("c1.xhtml", "p2", "2");
  sink.discard();
  EXPECT_FALSE(listExists());
}

TEST_F(PageListSinkTest, DiscardIsFinalAndTheDestructorWritesNothingBack) {
  {
    PageListSink sink(dir.string());
    sink.addEntry("c1.xhtml", "p1", "1");
    sink.discard();
    sink.addEntry("c1.xhtml", "p2", "2");
    sink.finalize();
  }
  EXPECT_FALSE(listExists());
}

TEST_F(PageListSinkTest, FinalizeKeepsAListWithEntries) {
  {
    PageListSink sink(dir.string());
    sink.addEntry("c1.xhtml", "p1", "1");
    sink.finalize();
  }
  EXPECT_TRUE(listExists());
}

}  // namespace
