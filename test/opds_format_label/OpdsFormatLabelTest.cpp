#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../src/activities/browser/OpdsFormatLabel.h"

// Every test goes through buildOpdsFormatSelectionLabels, the entry point
// OpdsBookBrowserActivity actually calls to label its format-selection menu.

namespace {
OpdsAcquisitionLink makeLink(const char* href, const char* formatKey) {
  return OpdsAcquisitionLink{href, "application/epub+zip", formatKey, ".epub"};
}

using Labels = std::vector<std::string>;
}  // namespace

TEST(OpdsFormatLabel, UniqueFormatUsesBaseLabel) {
  const std::vector<OpdsAcquisitionLink> links{makeLink("/books/example.epub", "epub")};

  ASSERT_EQ(buildOpdsFormatSelectionLabels(links, "catalog.example.com"), (Labels{"EPUB"}));
}

TEST(OpdsFormatLabel, DuplicateAbsoluteUrlsIncludeHostname) {
  const std::vector<OpdsAcquisitionLink> links{
      makeLink("https://mirror-a.example.com/books/example.epub", "epub"),
      makeLink("https://mirror-b.example.com/books/example.epub", "epub"),
  };

  ASSERT_EQ(buildOpdsFormatSelectionLabels(links, "catalog.example.com"),
            (Labels{"EPUB - mirror-a.example.com", "EPUB - mirror-b.example.com"}));
}

TEST(OpdsFormatLabel, DuplicateRootRelativeUrlsUseServerHostname) {
  const std::vector<OpdsAcquisitionLink> links{
      makeLink("/opds/download/1/epub", "epub"),
      makeLink("/opds/download/2/epub", "epub"),
  };

  ASSERT_EQ(buildOpdsFormatSelectionLabels(links, "https://catalog.example.com/opds"),
            (Labels{"EPUB - catalog.example.com (1)", "EPUB - catalog.example.com (2)"}));
}

TEST(OpdsFormatLabel, DuplicateRelativeUrlsUseServerHostname) {
  const std::vector<OpdsAcquisitionLink> links{
      makeLink("download/1.epub", "epub"),
      makeLink("download/2.epub", "epub"),
  };

  ASSERT_EQ(buildOpdsFormatSelectionLabels(links, "catalog.example.com/opds"),
            (Labels{"EPUB - catalog.example.com (1)", "EPUB - catalog.example.com (2)"}));
}

TEST(OpdsFormatLabel, DuplicateAbsoluteUrlsSameHostnameIncludeNumbering) {
  const std::vector<OpdsAcquisitionLink> links{
      makeLink("https://mirror.example.com/books/example.epub", "epub"),
      makeLink("https://mirror.example.com/books/example-copy.epub", "epub"),
  };

  ASSERT_EQ(buildOpdsFormatSelectionLabels(links, "catalog.example.com"),
            (Labels{"EPUB - mirror.example.com (1)", "EPUB - mirror.example.com (2)"}));
}

// Duplicate detection is per format: a format offered once keeps its bare label even
// when another format in the same list is decorated with hostnames and numbering.
TEST(OpdsFormatLabel, UniqueFormatBesideDuplicatesKeepsBaseLabel) {
  const std::vector<OpdsAcquisitionLink> links{
      makeLink("https://mirror.example.com/books/example.epub", "epub"),
      makeLink("https://mirror.example.com/books/example-copy.epub", "epub"),
      makeLink("/books/example.txt", "txt"),
  };

  ASSERT_EQ(buildOpdsFormatSelectionLabels(links, "https://catalog.example.com/opds"),
            (Labels{"EPUB - mirror.example.com (1)", "EPUB - mirror.example.com (2)", "TXT"}));
}
