#pragma once

#include <string>

/**
 * Per-book directory for BookOrbit sync state (stats queue, highlight/bookmark sync
 * bookkeeping), keyed by the book's content hash: "/.crosspoint/book_<hash>".
 *
 * Content-keyed rather than path-keyed so a moved or renamed file keeps its unsynced state,
 * and outside the epub_* cache folders so "Clear Cache" (which deletes only those) does not
 * throw away reading sessions that have not reached the server yet. Same layout as
 * CrossInk-Bookorbit, so a card carried over from that firmware keeps its queues.
 */
namespace BookOrbitBookState {

// Creates the directory on first use. Empty string when the book cannot be hashed.
std::string dirFor(const std::string& bookPath);

}  // namespace BookOrbitBookState
