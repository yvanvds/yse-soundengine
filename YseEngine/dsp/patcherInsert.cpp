#include "patcherInsert.hpp"
#include "../patcher/patcher.hpp"
#include "../patcher/patcherImplementation.h"

using namespace YSE::DSP;

patcherInsert::patcherInsert(YSE::patcher& p) : patch(&p) {}

void patcherInsert::create() {
  // Nothing to allocate here: the wrapped patcher already owns its graph and
  // output buffers, built by the caller before this insert was attached.
}

void patcherInsert::process(MULTICHANNELBUFFER& buffer) {
  createIfNeeded();
  if (patch == nullptr) return;
  // Reach through to the implementation to render the graph in place. patcher's
  // pimpl is private; patcherInsert is a friend (patcher.hpp).
  PATCHER::patcherImplementation* impl = patch->pimpl;
  if (impl == nullptr) return; // patcher::create() was never called
  impl->ProcessAsInsert(buffer);
}
