#pragma once
#include <string>
#include <vector>

// Sidecar files: companions sitting beside a book, sharing its name with a
// different extension. The firmware prefers them over the equivalent data
// embedded in the book, so "Some Book.jpg" overrides its cover and
// "Some Book.opf" overrides its metadata (see docs/sidecar-files.md).
//
// This is the single definition of what counts as a sidecar. It used to be
// spread across three places - the cover resolver, the metadata resolver and
// the move-to-/COMPLETED extension list - which is precisely how .opf came to
// be readable by the reader but left behind when a finished book moved.
//
// ADDING A NEW KIND OF SIDECAR MEANS ADDING IT HERE AND NOWHERE ELSE.
namespace SidecarFiles {

// Cover images, in resolution order: the first one that exists wins.
inline constexpr const char* kCoverExtensions[] = {".jpg", ".jpeg", ".png", ".bmp", ".JPG", ".JPEG", ".PNG", ".BMP"};
// Calibre-style metadata OPF.
inline constexpr const char* kMetadataExtensions[] = {".opf", ".OPF"};

// Sidecars whose name APPENDS to the book's full name instead of replacing its extension:
// "Some Book.epub" -> "Some Book.epub.rights", not "Some Book.rights". The content-protection
// rights document is delivered this way (docs/protected-content-plan.md) precisely so the
// book beside it stays byte-identical to what the server sent.
//
// Two kinds of sidecar therefore exist, and that is why callers that move or delete a book
// should use existingPaths()/movePairs() below rather than building names from basePath():
// only those can express both.
inline constexpr const char* kFullNameSuffixes[] = {".rights", ".RIGHTS"};

// "/Books/Some Book.epub" -> "/Books/Some Book". Empty when the path carries no
// extension of its own - a bare name, or one whose only dot belongs to a parent
// directory ("/My.Books/untitled"), which must not be mistaken for one.
std::string basePath(const std::string& bookPath);

// Full path of the first existing sidecar of that kind, or "" when there is
// none. Both hit the filesystem once per candidate extension.
std::string coverPath(const std::string& bookPath);
std::string metadataPath(const std::string& bookPath);

// Full paths of every sidecar that actually exists beside this book - covers, metadata and
// full-name suffixes alike. For callers that must treat them as a set rather than resolve one.
std::vector<std::string> existingPaths(const std::string& bookPath);

// Source/destination pairs for a book moving from `bookPath` to `newBookPath`, covering every
// sidecar that exists. This is the form movers should use: it handles both naming rules, so a
// caller cannot carry the cover and strand the rights document by constructing one of the two
// names by hand.
std::vector<std::pair<std::string, std::string>> movePairs(const std::string& bookPath, const std::string& newBookPath);

// Deletes every sidecar beside this book. Returns how many were removed. Call it when the book
// itself is deleted: a stranded sidecar is not just clutter, because a later book saved under
// the same name would inherit it - including a rights document that would make an ordinary
// book look protected.
size_t removeAll(const std::string& bookPath);

}  // namespace SidecarFiles
