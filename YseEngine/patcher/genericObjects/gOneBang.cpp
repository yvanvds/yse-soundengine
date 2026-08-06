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
      "Lets one bang through and then closes until it is re-armed from the right inlet — Max's "
      "onebang, and the standard one-shot: fire a sound on the first trigger of a burst and ignore "
      "the rest until something explicitly re-enables it. Max: 'Allows a bang in the left inlet to "
      "pass through ONLY if a bang has been received in the right inlet. After that, a bang in the "
      "left inlet will not get through again until a bang has been received again in the right "
      "inlet.' The patcher could already count triggers with .counter and compare them with .sel "
      "or "
      ".past, but expressing 'once, then not again until I say so' that way takes three boxes and "
      "leaves the re-arm implicit in the numbers, where here it is a patch cord. The whole object "
      "is one bit of state and two properties of it are worth knowing. The right inlet is "
      "idempotent: arming an already-armed gate does not bank a second pass, because Max says the "
      "left inlet passes 'only if it has received a bang in its right inlet since the last time it "
      "sent out a bang' — a since, not a count — so five arms followed by five triggers is one "
      "pass "
      "and four rejects, never five passes. And nothing is ever swallowed: every message the left "
      "inlet accepts produces exactly one bang, on one outlet or the other, so the object gates "
      "where a bang goes rather than whether one happens. That is what makes the second outlet "
      "worth wiring. There are two of them, which the object's name does not suggest — Max: "
      "'Otherwise, it sends a bang out its right outlet' — so a blocked bang is reported as a 'you "
      "were too early' signal rather than lost, and the two outlets together are a complete "
      "accounting of the left inlet's traffic. Exactly one fires per message, so unlike .trigger, "
      ".bangbang, .mean or .peak there is no right-to-left firing order to respect. Everything is "
      "a "
      "bang: Max gives int, float and list the description 'Same as a bang' and anything "
      "'Converted to bang', so the payload is documented as discarded, and a 0 on the left inlet "
      "triggers the gate like anything else. That is worth stating because the number reads like a "
      "'don't' — this is not .gate, and a value of 0 does not mean off anywhere in it, on either "
      "inlet; a patch that wants a numeric gate wants .gate. The word 'stop' on the left inlet is "
      "Max's manual disarm ('Undoes the effect of a bang in the right inlet'), silent, and it is "
      "the only word this object knows. Elsewhere in the family a symbol is a message the object "
      "would ignore anyway, so adding 'reset' costs nothing; here a symbol is a trigger, so every "
      "word learned is a bang silently no longer passed — and 'stop' plus a message on the right "
      "inlet already reach the entire state space. It is bare, as .change's 'mode' and .past's "
      "'clear' are: 'stop 1' is a list, and a list is a bang. On the right inlet 'stop' arms "
      "rather "
      "than disarming, because Max scopes the message to the left inlet while marking int, float, "
      "list and anything 'in either inlet', and because the reference describes the arming inlet "
      "with no conditions at all — a patch is entitled to treat a cord into it as 'the gate is now "
      "open', which would hold only for inspected message text if one string out of all possible "
      "strings closed it instead. The object starts disarmed, which is Max's default and also the "
      "safe one, since an object that started armed would let a patch load fire a bang nobody "
      "armed "
      "it for; a non-zero creation argument starts it armed, as Max's does. The argument is a "
      "flag, "
      "so only its zero-ness is read, and it is read from the float as it arrived rather than from "
      "a truncated int: Max's int argument would make 0.5 a zero, but this patcher has one numeric "
      "type and a flag that flipped on the third decimal place of a number nobody meant as a value "
      "would be a trap. A token that is not a whole finite number is not an argument, so '.onebang "
      "wobble' and '.onebang inf' both start disarmed — non-finite is 'not a number' in this "
      "family, as .change inf starting at 0 already establishes. Calculate() does nothing: the "
      "object is driven by its inlets, and emitting on a DSP tick would re-fire the gate every "
      "block, which for a one-shot is the exact failure it exists to prevent.");
  ADD_CATEGORY(pCategory::GENERIC);
  INLET_DOC(0, "in",
            "Bang, int, float or list — whatever arrives is discarded and treated as a bang. If "
            "the gate is armed a bang leaves outlet 0 and the gate closes; if it is not, a bang "
            "leaves outlet 1 instead. Every message produces exactly one bang, on one outlet or "
            "the other. Also accepts the bare word 'stop', Max's manual disarm, which is silent; "
            "'stop' with anything after it is an ordinary list and so an ordinary trigger.",
            "any");
  INLET_DOC(1, "arm",
            "Bang, int, float or list — anything at all re-arms the gate, so the next message on "
            "the left inlet passes. Silent, and idempotent: arming twice does not bank a second "
            "pass. Note that 'stop' arms here like any other message; the disarm lives on the left "
            "inlet, where Max documents it.",
            "any");
  OUTLET_DOC(0, "pass",
             "A bang, when the left inlet fired while the gate was armed. The gate closes as this "
             "is sent, so anything reached from here already sees a disarmed object.",
             "bang");
  OUTLET_DOC(1, "reject",
             "A bang, when the left inlet fired while the gate was closed — the 'you were too "
             "early' signal. Wire it to count or report the triggers that did not pass; leave it "
             "unconnected to get a plain one-shot.",
             "bang");
  PARAM_DOC("initial", "0",
            "Whether the gate starts open. A non-zero argument lets the first message on the left "
            "inlet pass, as Max's does; with no argument, a zero one, or a token that is not a "
            "whole finite number ('.onebang wobble', '.onebang inf'), the object starts closed and "
            "the first message goes out the reject outlet. Only the zero-ness is read, from the "
            "float as it arrived, so '.onebang 0.5' starts open where Max's int argument would "
            "truncate it to a zero.",
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
