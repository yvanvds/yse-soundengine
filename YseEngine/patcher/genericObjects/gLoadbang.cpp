// `.loadbang` (issue #547). See gLoadbang.h for the design; this file is two
// sends and a counter — the object's whole difficulty lives in *when* it is
// called, which is patcherImplementation::LoadbangObjects.
#include "gLoadbang.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gLoadbang

namespace {

  constexpr char kInletDoc[] =
      "A bang here makes the object send its bang, exactly as a load does — Max's 'sending a bang "
      "message to a loadbang object causes it to output a bang message'. It is the manual trigger, "
      "and it is the answer to the one case a load cannot cover: an object added to a patch that "
      "is already running never receives a loadbang, because at creation time it has no cords to "
      "send down and re-firing on a later edit would undo whatever the patch has done since. A "
      "host that has just built a graph object by object therefore initialises it by banging its "
      ".loadbang objects, and a host that loaded one from JSON does not have to.";

  constexpr char kOutletDoc[] =
      "A bang, sent once when the patcher finishes loading — Max's 'sent automatically when the "
      "patch is loaded' — and again for every bang that arrives at the inlet. The load bang leaves "
      "here only after the whole parsed graph has been built, wired and published, so everything "
      "the file describes is already in place and reachable when it travels; nothing downstream "
      "can see a half-built patch. The order in which two .loadbang objects in one patch fire is "
      "not defined, as in Max: if one initialisation has to precede another, put a .trigger "
      "between them.";

} // namespace

CONSTRUCT() {
  // One inlet, Max's manual trigger.
  ADD_IN_0;
  REG_BANG_IN(BangIn);

  // One bang outlet.
  ADD_OUT_BANG;

  // No creation arguments — Max: "None." Nothing to persist, so a save and a
  // load produce the same object, which then fires because it was loaded.

  ADD_DESCRIPTION(
      "Sends a bang once the patcher has finished loading — Max's 'loadbang', which 'outputs a "
      "bang automatically when the file is opened or when the patch is part of another file that "
      "is opened'. It is what lets a saved patch describe its own starting state: before it "
      "existed, a graph restored from JSON was a graph plus a list of things the host had to "
      "remember to do to it afterwards, because every parameter that needed initialising had to be "
      "pushed in from outside. Pair it with .loadmess, which is the same statement with a payload. "
      "The firing point is the whole of the object. The bang is sent at the end of ParseJSON, "
      "after the parsed graph has been compiled and published with a single atomic swap — not from "
      "a constructor, where the cords do not exist yet and the bang would reach nothing, and not "
      "part-way through the build, where it would reach some of the patch and not the rest with "
      "which part depending on the order the file happens to list its objects in. Only after the "
      "publish is the patch the patch the file describes. The pass runs on the control thread, "
      "outside the patcher's lock, so a patch whose initialisation travels through a .forward or a "
      ".qlist loads rather than deadlocking. The order in which two .loadbang objects fire is not "
      "defined, as in Max; use a .trigger when one initialisation must precede another. An object "
      "created live through CreateObject never receives a loadbang and stays silent until the "
      "patch is saved and loaded again: at creation time it has no cords, and firing on a later "
      "edit would re-run the patch's initialisation every time the patch was touched, undoing "
      "whatever had happened since. The inlet is the manual trigger for that case — a bang in "
      "makes it output, which is Max's documented behaviour. One inlet, one bang outlet, no "
      "creation arguments. Calculate() does nothing, and neither send allocates, locks or blocks.");
  ADD_CATEGORY(pCategory::GENERIC);

  INLET_DOC(0, "trigger", kInletDoc, "bang");
  OUTLET_DOC(0, "out", kOutletDoc, "bang");
}

void gLoadbang::Loadbang(YSE::THREAD thread) {
  // The load has finished and the graph is published. Everything downstream of
  // this outlet exists and is wired, so the bang reaches the patch the file
  // describes rather than a partial one.
  //
  // The tag is the caller's T_GUI and is passed straight through: this is the
  // control thread, and T_GUI is "set the state and let the block's own
  // traversal render what you caused" — which is exactly what an initialisation
  // message is for.
  fired.fetch_add(1, std::memory_order_relaxed);
  outputs[0].SendBang(thread);
}

BANG_IN(BangIn) {
  (void)inlet;
  // Max: "Sending a bang message to a loadbang object causes it to output a
  // bang message." Deliberately identical to the load path rather than a
  // near-copy of it — a host initialising a live-built graph must get the same
  // thing a load would have sent, or the two ways of starting a patch would not
  // start it the same way.
  fired.fetch_add(1, std::memory_order_relaxed);
  outputs[0].SendBang(thread);
}

#undef className
