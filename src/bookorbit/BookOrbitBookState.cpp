#include "BookOrbitBookState.h"

#include <HalStorage.h>
#include <KOReaderDocumentId.h>
#include <Logging.h>

namespace BookOrbitBookState {

std::string dirFor(const std::string& bookPath) {
  // Cached per book by KOReaderDocumentId, so this reads the file only the first time.
  const std::string hash = KOReaderDocumentId::calculate(bookPath);
  if (hash.empty()) {
    return "";
  }
  const std::string dir = "/.crosspoint/wr_book_" + hash;
  if (!Storage.exists(dir.c_str()) && !Storage.mkdir(dir.c_str())) {
    LOG_ERR("BookOrbit", "Cannot create book state dir: %s", dir.c_str());
    return "";
  }
  return dir;
}

}  // namespace BookOrbitBookState
