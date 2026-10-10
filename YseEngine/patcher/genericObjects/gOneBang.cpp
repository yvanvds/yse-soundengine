#include "gOneBang.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <string>

using namespace YSE::PATCHER;

#define className gOneBang

CONSTRUCT() {
  // The hot inlet. Every message type Max routes through `anything`, which is
  // all of them, and all four do the same thing — Max gives int, float and list
  // the description "Same as a bang" and anything "Converted to bang", so the
  // payload is documented as discarded rather than merely unused.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // Max's arming inlet. Cold: it never emits, it only opens the gate.
  ADD_IN_1;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // Two outlets, which the object's name does not suggest and its summary line
  // does not mention. Max: "it sends a bang out its left outlet only if it has
  // received a bang in its right inlet since the last time it sent out a bang.
  // Otherwise, it sends a bang out its right outlet." A blocked bang is
  // reported, not dropped.
  ADD_OUT_BANG;
  ADD_OUT_BANG;

  // The creation argument is a flag rather than a value, but it still arrives
  // through a parse callback rather than as a bare parameter field: `armed` is
  // derived from it and there is no other hook a plain scalar write would run,
  // so a saved `.onebang 1` would come back disarmed. The clear callback is
  // what makes `SetParams("")` return the object to Max's no-argument state
  // rather than leaving the previous argument's arming in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(initialArg);

  // Max's default: no argument means the first trigger does *not* pass. See
  // "Where the object starts" in the header for why disarmed is also the right
  // default on its own terms — an object that started armed would let a patch
  // load fire one bang nobody armed it for.
  initialArmed = false;
  armed = false;

  ADD_DESCRIPTION(
      "A one-shot: lets one message through as a bang from the left outlet, then sends every "
      "further message out the right outlet until the right inlet re-arms it.");
  ADD_CATEGORY(pCategory::CONTROL);
  INLET_DOC(0, "in",
            "Bang, int, float or list, treated as a bang: it leaves outlet 0 if the gate is armed, "
            "outlet 1 if not. The bare word 'stop' closes the gate silently.",
            "any");
  INLET_DOC(1, "arm", "Anything here arms the gate, silently. Arming twice still passes only once.",
            "any");
  OUTLET_DOC(0, "pass", "A bang when a message arrives while the gate is armed. The gate closes.",
             "bang");
  OUTLET_DOC(1, "reject", "A bang when a message arrives while the gate is closed.", "bang");
  PARAM_DOC("initial", "0",
            "Non-zero starts the gate armed, so the first message passes. Closed by default.",
            "0 or non-zero");
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave the object in
  // Max's no-argument state rather than still armed by the previous one.
  initialArg.clear();
  initialArmed = false;
  armed = false;
}

PARM_PARSE() {
  // Runs on the control thread once the creation parameter has been read.
  // Deriving `armed` here rather than in the constructor is what lets a saved
  // `.onebang 1` come back ready to pass its first bang.
  initialArmed = false;

  float number = 0.f;
  // Strict on purpose — the shared reader, not ExprParseFloatList, which would
  // read `1abc` as 1 and fold `1e999` to 0. A token that is not wholly a finite
  // number is not an argument at all, so it leaves the documented default in
  // place instead of arming the object on a symbol.
  if (ReadNumericToken(initialArg, number)) {
    // Max: "A non-zero argument sets onebang to permit a bang to be sent out
    // the left outlet the first time a bang is received in the left inlet."
    // Zero-ness of the float as it arrived, not of a truncated int — see the
    // header on why 0.5 arms here and would not in Max.
    initialArmed = (number != 0.f);
  }

  armed = initialArmed;
}

void gOneBang::Trigger(YSE::THREAD thread) {
  // **The object.** One bit read, one bit written, one bang sent.
  const bool pass = armed;

  // Closed *before* the send, so nothing reached from outlet 0 can observe the
  // object still armed. That is not decoration: the patcher's send path calls
  // the target inlet directly with no queue in between, so a patch that loops
  // outlet 0 back into this inlet re-enters here inside the SendBang — and with
  // the store after the send it would find the gate still open and pass again,
  // which for a one-shot is unbounded recursion rather than a stale read.
  armed = false;

  // Max: "it sends a bang out its left outlet only if it has received a bang in
  // its right inlet since the last time it sent out a bang. Otherwise, it sends
  // a bang out its right outlet." Exactly one of the two, always one of the
  // two: the object gates where a bang goes, not whether one happens.
  outputs[pass ? 0 : 1].SendBang(thread);
}

BANG_IN(SetBang) {
  if (inlet == 1) {
    // Max: "Resets onebang to permit a bang to be sent out the next time a bang
    // is received in the left inlet." Unconditional and silent, so arming an
    // already-armed gate is a no-op rather than a second banked pass.
    armed = true;
    return;
  }
  if (inlet != 0) return;
  Trigger(thread);
}

INT_IN(SetInt) {
  // Max: "In either inlet: Same as a bang." The value is discarded, including a
  // 0 — this is not .gate, and 0 does not mean off on either inlet.
  (void)value;
  SetBang(inlet, thread);
}

FLOAT_IN(SetFloat) {
  (void)value;
  SetBang(inlet, thread);
}

LIST_IN(SetList) {
  // Max scopes `stop` to the left inlet while marking int, float, list and
  // anything "in either inlet", so on the arming inlet the word is one more
  // thing converted to a bang. See "stop on the right inlet arms" in the
  // header: the reference describes that inlet with no conditions, and a patch
  // is entitled to treat a cord into it as "the gate is now open".
  if (inlet == 0) {
    // Max: "In left inlet: Undoes the effect of a bang in the right inlet."
    // Silent — the reference lists only `bang` under Output. Compared bare, as
    // .change's `mode` and .past's `clear` are, so `stop 1` is a list and a
    // list is a bang; MatchWord cannot answer this one, since it requires a
    // separator after the word. No allocation: this is a length check and a
    // memcmp against a literal.
    if (value == "stop") {
      armed = false;
      return;
    }
  }

  // Everything else, on either inlet, is Max's `anything` — "Converted to
  // bang". The text is never parsed, so there is no number to read and no
  // symbol to look up.
  (void)value;
  SetBang(inlet, thread);
}

GUI_VALUE() {
  // The one bit of state, and the only thing an editor could usefully show:
  // whether the next trigger will pass.
  return armed ? "1" : "0";
}
