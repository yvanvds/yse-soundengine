// `patcher` — the subpatcher façade (issue #545). See gSubpatcher.h for the
// model; this file is a description and a name parameter and nothing else,
// because the object's whole behaviour lives in patcherImplementation:
// boundary resolution in Connect / Disconnect, the containment subtree in
// DeleteObject, and the container annotation in DumpJson / ParseJSON.
#include "gSubpatcher.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gSubpatcher

CONSTRUCT() {
  // No pins — deliberate, and explained in the header: a subpatcher's boundary
  // belongs to its `.inlet` / `.outlet` objects, so that adding one is an
  // ordinary object creation rather than a pin-count change on an object the
  // audio thread is walking.
  //
  // One free-text creation argument, the subpatch's name (issue #1260). It is
  // a string, so a live SetParams rebuilds the object (#234), and
  // ReplaceObjectUnlocked re-points everything inside at the replacement.
  ADD_PARAM(name);

  ADD_DESCRIPTION(
      "A subpatcher: groups part of a patch into one object that the parent connects to by pin "
      "number. Its pins are the .inlet, .outlet, ~inlet and ~outlet objects placed inside it.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

  PARAM_DOC("name", "",
            "The subpatch's name, for a host to show as its title. Any text, spaces included; "
            "the object keeps it as given and does nothing else with it.",
            "any text");
}

#undef className
