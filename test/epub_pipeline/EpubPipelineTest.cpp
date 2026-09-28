// Full-pipeline equivalence tests over the synthetic corpus:
//  1. Warm-cache equivalence — rebuilding over the book-level caches a cold
//     run left behind dumps identically to that run.
//  2. Golden equivalence — two cold builds of every synthetic corpus book dump
//     identically to each other and to the committed golden. Regenerate
//     intentionally changed goldens with: UPDATE_GOLDENS=1 ctest -R EpubPipeline
//
// Every book runs with font-size normalization OFF (the tight ±3% float-rounding
// dead zone). The books whose layout normalization actually changes also run
// with it ON (the ±10% band that snaps publisher near-body
// <span font-size:0.92em> wrappers back to native size), against a golden of
// their own: <book>.golden.txt for OFF, <book>_norm.golden.txt for ON.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "PipelineRunner.h"

namespace fs = std::filesystem;

namespace {

std::string freshCacheDir(const std::string& tag) {
  const auto dir = fs::temp_directory_path() / "epub_pipeline_test" / tag;
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

// One corpus book under one font-size-normalization setting.
struct Case {
  std::string epub;
  bool fontSizeNormalization = false;
};

// Without this gtest dumps the struct as raw bytes in failure messages.
void PrintTo(const Case& c, std::ostream* os) {
  *os << fs::path(c.epub).filename().string() << " [fontSizeNormalization=" << (c.fontSizeNormalization ? "on" : "off")
      << "]";
}

std::string stem(const std::string& path) { return fs::path(path).stem().string(); }

// The corpus books whose layout normalization changes. For every other book the ON dump is
// byte-identical to OFF, so an ON case would only re-assert the OFF golden under a second name.
const char* const kNormalizationSensitiveBooks[] = {"test_font_normalization", "test_font_sizes"};

// Distinguishes the two variants' cache dirs and golden files. OFF keeps the
// historical unsuffixed golden name so its committed layout stays reviewable
// across the normalization change.
std::string variantSuffix(const Case& c) { return c.fontSizeNormalization ? "_norm" : ""; }

std::string runOnce(const Case& c, const std::string& cacheDir) {
  pipeline_harness::Profile profile;
  profile.fontSizeNormalization = c.fontSizeNormalization;
  std::ostringstream dump;
  const bool ok = pipeline_harness::runAndDump(c.epub, cacheDir, profile, dump);
  EXPECT_TRUE(ok) << "pipeline failed for " << c.epub
                  << " (fontSizeNormalization=" << (c.fontSizeNormalization ? "on" : "off") << ")\n"
                  << dump.str();
  return dump.str();
}

// Unique per (book, variant) so the two variants never share a cache dir.
std::string caseCacheDir(const Case& c, const std::string& tag) {
  return freshCacheDir(stem(c.epub) + variantSuffix(c) + "_" + tag);
}

bool readFile(const fs::path& path, std::string& out) {
  std::ifstream in(path);
  if (!in) return false;
  std::stringstream bytes;
  bytes << in.rdbuf();
  out = bytes.str();
  return true;
}

class EpubPipelineTest : public testing::TestWithParam<Case> {};

// runAndDump always rebuilds every section, so the second run is not a page cache served without a
// build. What it does read back instead of the archive is the book-level state the first run left:
// book.bin, the compiled CSS index, the image manifest, the banked chapter XHTML and the footnote
// store with its resolved-spine bits.
TEST_P(EpubPipelineTest, RebuildOverWarmBookCacheMatchesColdRun) {
  const Case c = GetParam();
  const std::string cacheDir = caseCacheDir(c, "warm");
  const std::string cold = runOnce(c, cacheDir);
  const std::string warm = runOnce(c, cacheDir);
  EXPECT_EQ(cold, warm) << "rebuilding " << c.epub << " over its warm book cache changed the layout";
}

// Built twice, into separate cache dirs: a nondeterministic build (an uninitialised field, a
// pointer-ordered container) can match the golden once by luck, and must not be allowed to write one.
TEST_P(EpubPipelineTest, MatchesGolden) {
  const Case c = GetParam();
  const std::string dump = runOnce(c, caseCacheDir(c, "a"));
  ASSERT_EQ(dump, runOnce(c, caseCacheDir(c, "b"))) << "two cold builds of " << c.epub << " diverged";
  const fs::path goldenPath = fs::path(GOLDEN_DIR) / (stem(c.epub) + variantSuffix(c) + ".golden.txt");

  if (std::getenv("UPDATE_GOLDENS")) {
    std::ofstream(goldenPath) << dump;
    GTEST_SKIP() << "golden regenerated: " << goldenPath;
  }
  std::string golden;
  ASSERT_TRUE(readFile(goldenPath, golden))
      << "missing golden " << goldenPath << " — run with UPDATE_GOLDENS=1 to create it";
  EXPECT_EQ(golden, dump) << "layout drift vs golden for " << c.epub
                          << " (fontSizeNormalization=" << (c.fontSizeNormalization ? "on" : "off")
                          << ") — if intentional, regenerate with UPDATE_GOLDENS=1 and explain in the commit";

  // The ON case is only worth its golden while normalization still changes this book's layout.
  if (c.fontSizeNormalization) {
    std::string offGolden;
    ASSERT_TRUE(readFile(fs::path(GOLDEN_DIR) / (stem(c.epub) + ".golden.txt"), offGolden));
    EXPECT_NE(offGolden, dump) << c.epub << " no longer lays out differently with normalization ON; "
                               << "drop it from kNormalizationSensitiveBooks and delete its _norm golden";
  }
}

// Every corpus book with normalization OFF, plus ON for the books it changes.
std::vector<Case> corpusCases() {
  std::vector<std::string> paths;
  for (const auto& entry : fs::directory_iterator(CORPUS_DIR)) {
    if (entry.path().extension() == ".epub") paths.push_back(entry.path().string());
  }
  std::sort(paths.begin(), paths.end());

  std::vector<Case> cases;
  for (const auto& path : paths) {
    cases.push_back(Case{path, /*fontSizeNormalization=*/false});
    const auto* const sensitive =
        std::find(std::begin(kNormalizationSensitiveBooks), std::end(kNormalizationSensitiveBooks), stem(path));
    if (sensitive != std::end(kNormalizationSensitiveBooks)) {
      cases.push_back(Case{path, /*fontSizeNormalization=*/true});
    }
  }
  return cases;
}

INSTANTIATE_TEST_SUITE_P(SyntheticCorpus, EpubPipelineTest, testing::ValuesIn(corpusCases()),
                         [](const testing::TestParamInfo<Case>& info) {
                           std::string name = stem(info.param.epub) + variantSuffix(info.param);
                           for (char& c : name)
                             if (!isalnum(static_cast<unsigned char>(c))) c = '_';
                           return name;
                         });

}  // namespace
