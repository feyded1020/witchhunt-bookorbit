// The update dialog's "what changed" text: pulling the release body out of GitHub's JSON as it
// streams past, in chunks that fall wherever the network puts them.
#include <gtest/gtest.h>

#include <string>

#include "network/ReleaseNotesScanner.h"
#include "sample_release.h"

namespace {
// Feeds `json` in fixed-size pieces, the way the HTTP callback delivers it.
std::string scanInChunks(const std::string& json, const size_t chunk) {
  ReleaseNotesScanner scanner;
  for (size_t i = 0; i < json.size() && !scanner.done(); i += chunk) {
    scanner.feed(json.data() + i, std::min(chunk, json.size() - i));
  }
  return scanner.text();
}
}  // namespace

TEST(ReleaseNotes, ReadsTheBodyFromARealGitHubResponse) {
  const std::string notes = scanInChunks(SAMPLE_RELEASE_JSON, 512);
  EXPECT_FALSE(notes.empty());
  EXPECT_NE(notes.find("See what changed before you update"), std::string::npos);
}

TEST(ReleaseNotes, SurvivesChunkBoundariesAnywhere) {
  // The key, the escapes and the terminator must all be recognised across a split.
  const std::string expected = scanInChunks(SAMPLE_RELEASE_JSON, 4096);
  for (const size_t chunk : {1u, 2u, 3u, 7u, 13u, 64u, 1024u}) {
    EXPECT_EQ(scanInChunks(SAMPLE_RELEASE_JSON, chunk), expected) << "chunk size " << chunk;
  }
}

TEST(ReleaseNotes, DecodesEscapesAndStopsAtTheClosingQuote) {
  const std::string json = R"({"tag_name":"1.0","body":"Line one\r\nLine two \"quoted\" and \\ back","x":1})";
  EXPECT_EQ(scanInChunks(json, 8), "Line one\nLine two \"quoted\" and \\ back");
}

TEST(ReleaseNotes, StopsAtTheCharacterCap) {
  std::string json = R"({"body":")" + std::string(ReleaseNotesScanner::NOTES_MAX + 50, 'x') + R"("})";
  const std::string notes = scanInChunks(json, 32);
  EXPECT_EQ(notes.size(), ReleaseNotesScanner::NOTES_MAX);
}

TEST(ReleaseNotes, EmptyWhenTheReleaseHasNoNotes) {
  EXPECT_EQ(scanInChunks(R"({"tag_name":"1.0","assets":[]})", 16), "");
}

TEST(ReleaseNotes, IgnoresAKeyThatMerelyLooksLikeTheBody) {
  // "somebody" ends in body; the quote before it is what makes the real key.
  const std::string json = R"({"somebody":"no","body":"yes"})";
  EXPECT_EQ(scanInChunks(json, 5), "yes");
}
