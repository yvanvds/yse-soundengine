#include "gBangBang.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>

using namespace YSE::PATCHER;

#define className gBangBang

CONSTRUCT() {
  // The one inlet. Every message type Max routes through `anything`, which is
  // all of them — and all four do the same thing, since the payload is thrown
  // away.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // The outlet count is the creation argument, so the ports are built in the
  // parse callback and a saved `.bangbang 5` comes back with five outlets. The
  // clear callback is what makes `SetParams("")` return the object to Max's
  // no-argument shape rather than leaving the previous count in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(outletArg);

  outletCount = DEFAULT_OUTLETS;
  ShapePorts();

  ADD_DESCRIPTION(
      "Sends a bang out every outlet, in right-to-left order, whenever anything arrives. Max's "
      "bangbang, and the bang-only degenerate case of .trigger: where .trigger converts the input "
      "per outlet, this discards it entirely, so a bang, an int, a float and a list all produce "
      "exactly the same N bangs. What it keeps is the ordering guarantee, which is the reason "
      "either object exists: outlet n-1 is sent first and outlet 0 last, and each send completes "
      "in full — the whole subgraph hanging off that outlet, depth first — before the next one "
      "starts, because the patcher's send path calls the target inlet directly with no queue in "
      "between. That makes 'do this, and only once it has finished, do that' expressible as a "
      "single box rather than as an assumption about how the patch happens to be wired. The order "
      "in which several patch cords from the same outlet are served is deliberately not "
      "guaranteed, as in Max; the answer to needing it is another .bangbang. The optional creation "
      "argument is the outlet count, not a format list — .bangbang 3 has three outlets where "
      ".trigger 3 has one outlet emitting the constant 3 — and Max's documented range of 1 to 40 "
      "is applied, so a larger count is capped at 40 rather than following .trigger's 256. A float "
      "argument is truncated towards zero, as Max converts it to an int; an argument that is not a "
      "whole finite number, or no argument at all, gives two outlets, which is Max's default shape "
      "and .trigger's. Calculate() does nothing: the object is driven by its inlet, and emitting "
      "on a DSP tick would re-fire the whole fan-out every block.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Bang, int, float or list. Whatever arrives is discarded and a bang is sent out every "
            "outlet, right to left.",
            "any");
  PARAM_DOC("outlets", "2",
            "How many outlets to build, between 1 and 40 — Max's documented range. A float is "
            "truncated towards zero; a count outside the range is clamped to it. With no argument, "
            "or with an argument that is not a whole finite number, there are two outlets.",
            "1-40");
}

void gBangBang::ShapePorts() {
  // Rebuilt rather than resized so that shrinking and growing take the same
  // path, and because the docs are per-outlet. Safe because every caller runs
  // before the object is wired or published — the constructor, and the two
  // parameter callbacks, which patcherImplementation::CreateObjectUnlocked runs
  // before AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
  outputs.clear();
  for (int i = 0; i < outletCount; i++) {
    ADD_OUT_BANG;
    outputs.back().SetDoc(OutletLabel(i),
                          "A bang, whatever arrived at the inlet. Outlet n-1 fires first and "
                          "outlet 0 last, and each send completes in full before the outlet to "
                          "its left is served.",
                          "bang");
  }
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one with no outlets at all.
  outletArg.clear();
  outletCount = DEFAULT_OUTLETS;
  ShapePorts();
}

PARM_PARSE() {
  outletCount = DEFAULT_OUTLETS;

  float number = 0.f;
  // Strict on purpose — see the header. `ExprParseFloatList` would read `3abc`
  // as 3 and fold `1e999` to 0, and a count of 0 is not a shape this object
  // has.
  if (ReadNumericToken(outletArg, number)) {
    // Clamped as a float before the conversion rather than after it: a value
    // far outside the int range (`1e30`) is a *large* count and belongs at the
    // ceiling, where ExprToInt would answer 0 for it and land it at the floor
    // instead. Inside the range the two orders agree, since Max truncates and
    // then clamps and truncation cannot move a value across a whole bound.
    if (number > (float)MAX_OUTLETS) {
      outletCount = MAX_OUTLETS;
    } else if (number < (float)MIN_OUTLETS) {
      outletCount = MIN_OUTLETS;
    } else {
      // Max: "Floats are converted to ints." Towards zero, as C truncates.
      outletCount = ExprToInt(number);
    }
  }

  ShapePorts();
}

void gBangBang::EmitAll(THREAD thread) {
  // **The object.** Right to left, and each SendBang returns only once the
  // whole subgraph behind that outlet has run — see the header on why that is a
  // guarantee rather than a coincidence. Walking forwards here would be the
  // single bug this object exists to prevent, so it is worth being loud: the
  // loop counts down.
  //
  // No allocation, no lock, no I/O: a bang carries no payload, so there is
  // nothing to convert or format on the way out.
  for (int i = outletCount - 1; i >= 0; i--)
    outputs[(std::size_t)i].SendBang(thread);
}

// All four handlers are the same object. Max lists bang, int, float and
// anything separately and gives each of them the identical description,
// "Causes a bang to be sent out all outlets in right-to-left order" — the
// payload is not merely unused, it is the documented behaviour that it is
// ignored.
BANG_IN(SetBang) {
  if (inlet != 0) return;
  EmitAll(thread);
}

INT_IN(SetInt) {
  if (inlet != 0) return;
  (void)value;
  EmitAll(thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  (void)value;
  EmitAll(thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;
  (void)value;
  EmitAll(thread);
}
