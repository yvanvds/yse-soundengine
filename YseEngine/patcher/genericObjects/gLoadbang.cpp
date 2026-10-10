// `.loadbang` (issue #547). See gLoadbang.h for the design; this file is two
// sends and a counter — the object's whole difficulty lives in *when* it is
// called, which is patcherImplementation::LoadbangObjects.
#include "gLoadbang.h"

#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gLoadbang

namespace {

  constexpr char kInletDoc[] =
      "A bang sends a bang, as a load does. Use it to initialise a patch built with CreateObject.";

  constexpr char kOutletDoc[] = "A bang when the patch has finished loading, and for each bang in.";

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
      "Sends a bang when the patch it is in has finished loading, so a saved patch can set up its "
      "own starting state. A bang at the inlet sends one by hand.");
  ADD_CATEGORY(pCategory::CONTROL);

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
