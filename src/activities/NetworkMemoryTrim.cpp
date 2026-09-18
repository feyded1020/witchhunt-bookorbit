#include "NetworkMemoryTrim.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>

void trimMemoryForNetworkSession(const GfxRenderer& renderer, const char* logTag) {
  if (auto* cache = renderer.getFontCacheManager()) {
    cache->clearCache();
  }
  // Reclaim the buffer first if the reader lent it out for a background page build. A lent
  // buffer reports hasSecondaryBuffer() == false, so without this the trim below would quietly
  // do nothing AND the later releaseFrameBuffers() would bail out on _secondaryLent — leaving
  // ~52 KB held for the whole session by the one code path that exists to free it. Returns
  // false and costs nothing when nothing is lent, which is the normal case.
  if (renderer.returnSecondaryBuffer()) {
    LOG_DBG(logTag, "Reclaimed a lent secondary framebuffer before trimming");
  }

  if (renderer.hasSecondaryBuffer()) {
    // Seed the controller baseline while frameBufferActive is still valid.
    if (!renderer.isX3()) renderer.syncRedRamFromFrameBuffer();
    if (renderer.releaseSecondaryBuffer()) {
      LOG_DBG(logTag, "Released secondary framebuffer before network session (~52 KB contiguous)");
      renderer.setSingleBufferFastDiff(true);
    }
  }
}
