#pragma once

#include <string>

/**
 * Per-book directory for BookOrbit sync state (stats queue, highlights, highlight/bookmark sync
 * bookkeeping), keyed by the book's content hash: "/.crosspoint/wr_book_<hash>".
 *
 * Content-keyed rather than path-keyed so a moved or renamed file keeps its unsynced state,
 * and outside the epub_* cache folders so "Clear Cache" (which deletes only those) does not
 * throw away reading sessions that have not reached the server yet.
 *
 * Deliberately NOT CrossInk-Bookorbit's "book_<hash>": that state belongs to CrossInk's device id
 * and its highlight/bookmark records point at files this firmware does not read. Inheriting it
 * would mix two devices' bookkeeping; starting clean lets the server send everything down.
 */
namespace BookOrbitBookState {

// Creates the directory on first use. Empty string when the book cannot be hashed.
std::string dirFor(const std::string& bookPath);

}  // namespace BookOrbitBookState
