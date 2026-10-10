// `patcher` — the subpatcher façade (issue #545). See gSubpatcher.h for the
// model; this file is a description and nothing else, because the object's
// whole behaviour lives in patcherImplementation: boundary resolution in
// Connect / Disconnect, the containment subtree in DeleteObject, and the
// container annotation in DumpJson / ParseJSON.
#include "gSubpatcher.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gSubpatcher

CONSTRUCT() {
  // No pins and no creation arguments — both deliberate, and both explained in
  // the header. The short version: a subpatcher's boundary belongs to its
  // `.inlet` / `.outlet` objects, so that adding one is an ordinary object
  // creation rather than a pin-count change on an object the audio thread is
  // walking; and an object with no registered parameters can never be sent
  // down the #234 structural-replacement path, which would otherwise swap the
  // pointer every contained object names as its container.

  ADD_DESCRIPTION(
      "A subpatcher: groups part of a patch into one object that the parent connects to by pin "
      "number. Its pins are the .inlet, .outlet, ~inlet and ~outlet objects placed inside it.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);
}

#undef className
