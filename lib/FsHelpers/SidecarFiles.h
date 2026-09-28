#pragma once
#include <cstddef>
#include <cstdint>
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
// metadataStamp() reads no further than this. It must cover every byte a reader
// of the sidecar takes in, or an edit past the cap would go unnoticed.
inline constexpr size_t kMetadataStampBytes = 16384;

// "/Books/Some Book.epub" -> "/Books/Some Book". Empty when the path carries no
// extension of its own - a bare name, or one whose only dot belongs to a parent
// directory ("/My.Books/untitled"), which must not be mistaken for one.
std::string basePath(const std::string& bookPath);

// Full path of the first existing sidecar of that kind, or "" when there is
// none. Both hit the filesystem once per candidate extension.
std::string coverPath(const std::string& bookPath);
std::string metadataPath(const std::string& bookPath);

// Fingerprint of the metadata sidecar's contents, for callers that keep a copy of
// what it produced and must notice when it changes. The recent-books list stores
// title and author when a book is opened, so a sidecar written afterwards never
// reached the home screen. 0 = no sidecar; an existing one, even an empty one,
// is never 0. A hash of the contents rather than the modification time: a device
// without a set clock stamps every write 1980-01-01, and an edit that keeps the
// length would keep the size too.
uint32_t metadataStamp(const std::string& bookPath);

// Extensions of every sidecar that actually exists beside this book, covers and
// metadata alike. For callers that must treat them as a set rather than resolve
// one - moving a book has to carry all of them, or it silently strands the
// cover and the metadata corrections behind.
std::vector<const char*> existingExtensions(const std::string& bookPath);

}  // namespace SidecarFiles
