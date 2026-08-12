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
      "Groups a region of a patch so the parent addresses it as one object — Max's 'patcher' (and "
      "its alias 'p'). Encapsulation is the point: without it every graph is flat, which caps how "
      "large a patch can get before it stops being readable and rules out reusable building blocks "
      "entirely. Contents are placed inside it with patcher::SetContainer and the boundary is made "
      "of .inlet and .outlet objects: connecting the parent to inlet N of a subpatcher records an "
      "ordinary cord straight to the '.inlet N' object inside it, and .outlet mirrors that "
      "outward, so the graph the engine compiles contains no subpatchers at all. Storage is flat "
      "and only the addressing is nested — every object in a patch, however deep, lives in one "
      "object set, takes a graph id from one id space and is compiled into one GraphState. Three "
      "things follow. The audio thread's traversal cost does not grow with nesting depth, because "
      "depth is not a thing it can see. An edit inside a subpatcher is an ordinary edit and goes "
      "through the same single atomic GraphState swap as one at the top level. And nothing about "
      "nesting is read on the audio thread at all — membership is a control-thread annotation used "
      "by Connect, Disconnect, DeleteObject and DumpJSON. This object has no inlets and no outlets "
      "of its own, which is a correctness requirement rather than an economy: the boundary changes "
      "whenever a .inlet is added, and a real pin vector would have to be resized on an object the "
      "render is concurrently walking. Use patcher::SubpatcherInlets / SubpatcherOutlets to read "
      "the boundary shape. Deleting a subpatcher deletes its contents transitively, and every "
      "object in the subtree receives Teardown before any of them is unwired. On load, contents "
      "fire their loadbangs in the same undefined-order pass as the top level: the whole tree is "
      "published in one swap, so 'loading has finished' becomes true for every level at the same "
      "instant. No creation arguments. Calculate() does nothing and is never reached.");
  ADD_CATEGORY(pCategory::GENERIC);
}

#undef className
