#include "BookOrbitBookState.h"

#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>

namespace BookOrbitBookState {

namespace {
// "/.crosspoint/book_<content hash>", or "" when the book cannot be hashed. The hash itself is
// cached per book by KOReaderDocumentId, so only the first call per book reads the file.
std::string dirName(const std::string& bookPath) {
  const std::string hash = KOReaderDocumentId::calculate(bookPath);
  // "wr_book_", never CrossInk's "book_": see the header -- sharing that directory would mix two
  // devices' sync bookkeeping and make the server read CrossInk's highlights as deleted.
  return hash.empty() ? std::string() : "/.crosspoint/wr_book_" + hash;
}
}  // namespace

std::string dirIfExists(const std::string& bookPath) {
  const std::string dir = dirName(bookPath);
  if (dir.empty() || !Storage.exists(dir.c_str())) return "";
  return dir;
}

std::string dirFor(const std::string& bookPath) {
  const std::string dir = dirName(bookPath);
  if (dir.empty()) {
    return "";
  }
  if (!Storage.exists(dir.c_str()) && !Storage.mkdir(dir.c_str())) {
    LOG_ERR("BookOrbit", "Cannot create book state dir: %s", dir.c_str());
    return "";
  }
  return dir;
}

}  // namespace BookOrbitBookState
