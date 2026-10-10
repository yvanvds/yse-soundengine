#include "gTrigger.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include <cstddef>
#include <string>

using namespace YSE::PATCHER;

#define className gTrigger

namespace {

  const char* KindDoc(gTrigger::Kind kind) {
    switch (kind) {
    case gTrigger::Kind::INT:
      return "The input as an int: a float is truncated toward zero, a list or a bang sends 0.";
    case gTrigger::Kind::FLOAT:
      return "The input as a float: a list or a bang sends 0.";
    case gTrigger::Kind::BANG:
      return "A bang, whatever arrived.";
    case gTrigger::Kind::LIST:
      return "A list unchanged; anything else sends the list 0.";
    case gTrigger::Kind::SYMBOL:
      return "Text (a list) unchanged; anything else sends the empty symbol.";
    case gTrigger::Kind::CONST_INT:
    case gTrigger::Kind::CONST_FLOAT:
    case gTrigger::Kind::CONST_SYMBOL:
      break;
    }
    return "This outlet's constant, sent on every input.";
  }

  const char* KindRange(gTrigger::Kind kind) {
    switch (kind) {
    case gTrigger::Kind::INT:
    case gTrigger::Kind::CONST_INT:
      return "any int";
    case gTrigger::Kind::FLOAT:
    case gTrigger::Kind::CONST_FLOAT:
      return "any float";
    case gTrigger::Kind::BANG:
      return "bang";
    case gTrigger::Kind::LIST:
    case gTrigger::Kind::SYMBOL:
    case gTrigger::Kind::CONST_SYMBOL:
      break;
    }
    return "any list";
  }

} // namespace

CONSTRUCT() {
  // The one inlet. Every message type Max routes through `anything`, which is
  // all of them.
  ADD_IN_0;
  REG_BANG_IN(SetBang);
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // The outlets are built from the slot table, so a saved `.trigger b i` comes
  // back with two correctly typed outlets. The clear callback is what makes
  // `SetParams("")` return the object to Max's no-argument shape rather than
  // leaving the previous argument list in place.
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  ADD_PARAM(formatArgs);

  zeroList = "0";
  nullSymbol.clear();

  // Max: "If there are no arguments, there are two outlets, both of which send
  // an int." Also the shape ClearParams() restores.
  ResetToDefaultSlots();
  ShapePorts();

  ADD_DESCRIPTION(
      "Sends its input out every outlet, right to left, converted per outlet to an int, a float, a "
      "bang, a list or text, or replaced by a constant. Use it to make the order of two branches "
      "explicit.");
  ADD_CATEGORY(pCategory::CONTROL);
  INLET_DOC(0, "in", "Bang, int, float or list, sent out every outlet, right to left.", "any");
  PARAM_DOC("formats", "i i",
            "One argument per outlet: 'i', 'f', 'b', 'l' or 's' to convert the input, or any other "
            "token as a constant. Two 'i' outlets by default, at most 256.",
            "any list of i/f/b/l/s letters and constants");
}

void gTrigger::ResetToDefaultSlots() {
  // Max: "If there are no arguments, there are two outlets, both of which send
  // an int."
  slots.clear();
  slots.push_back(Slot{Kind::INT, 0, 0.f, "i"});
  slots.push_back(Slot{Kind::INT, 0, 0.f, "i"});
}

void gTrigger::ShapePorts() {
  // Rebuilt rather than resized: the outlet *types* come from the arguments, so
  // there is nothing to append to. Safe because every caller runs before the
  // object is wired or published — the constructor, and the two parameter
  // callbacks, which patcherImplementation::CreateObjectUnlocked runs before
  // AssignGraphIds. A *live* SetParams never reaches here on a published
  // object: registering the callbacks makes ParamsNeedRebuild() true, so #234
  // replaces the object instead.
  outputs.clear();
  for (std::size_t i = 0; i < slots.size(); i++) {
    switch (slots[i].kind) {
    case Kind::INT:
    case Kind::CONST_INT:
      ADD_OUT_INT;
      break;
    case Kind::FLOAT:
    case Kind::CONST_FLOAT:
      ADD_OUT_FLOAT;
      break;
    case Kind::BANG:
      ADD_OUT_BANG;
      break;
    case Kind::LIST:
    case Kind::SYMBOL:
    case Kind::CONST_SYMBOL:
      ADD_OUT_LIST;
      break;
    }
    outputs.back().SetDoc(OutletLabel((int)i), KindDoc(slots[i].kind), KindRange(slots[i].kind));
  }
}

PARM_CLEAR() {
  // Runs on the control thread before the parameter string is re-read, and is
  // the whole of `SetParams("")`: Parameters::Set returns without calling the
  // parse callback for an empty argument, so this has to leave a usable object
  // behind rather than one with no outlets at all.
  formatArgs.clear();
  ResetToDefaultSlots();
  ShapePorts();
}

PARM_PARSE() {
  slots.clear();
  for (const std::string& token : formatArgs) {
    if (slots.size() >= (std::size_t)MAX_OUTLETS) break;
    // Parameters::Set splits on single spaces, so a run of them yields empty
    // tokens. An empty argument is not a format letter and not a constant
    // anyone typed; it would only cost an outlet that emits nothing useful.
    if (token.empty()) continue;

    if (token.size() == 1) {
      // The five format letters, lowercase and single-character as in Max.
      // Anything else — including 'I' or the word 'bang' — falls through to the
      // constant branch, which is the right reading: Max has no long spelling
      // for these, so a longer token is a symbol the user meant literally.
      switch (token[0]) {
      case 'i':
        slots.push_back(Slot{Kind::INT, 0, 0.f, token});
        continue;
      case 'f':
        slots.push_back(Slot{Kind::FLOAT, 0, 0.f, token});
        continue;
      case 'b':
        slots.push_back(Slot{Kind::BANG, 0, 0.f, token});
        continue;
      case 'l':
        slots.push_back(Slot{Kind::LIST, 0, 0.f, token});
        continue;
      case 's':
        slots.push_back(Slot{Kind::SYMBOL, 0, 0.f, token});
        continue;
      default:
        break;
      }
    }

    // Max: "When an int, float, or symbol is specified, the value is output as
    // a constant."
    float number = 0.f;
    if (ReadNumericToken(token, number)) {
      // The int-atom / float-atom test lives in pListArgs.h next to the reader
      // that agreed the token is a number; `.match` (#472) needs the same
      // answer to echo a value back in the spelling it arrived in.
      if (TokenLooksLikeFloat(token)) {
        slots.push_back(Slot{Kind::CONST_FLOAT, 0, number, token});
      } else {
        // Spelled as an int, so it is an int atom — but the token may still be
        // wider than an int (ReadNumericToken only promised a finite float), so
        // truncate through the range-checked conversion rather than casting.
        slots.push_back(Slot{Kind::CONST_INT, ExprToInt(number), 0.f, token});
      }
    } else {
      slots.push_back(Slot{Kind::CONST_SYMBOL, 0, 0.f, token});
    }
  }

  // An argument list of nothing but separators is a bare `.trigger`.
  if (slots.empty()) ResetToDefaultSlots();

  ShapePorts();
}

gTrigger::Kind gTrigger::SlotKind(int index) const {
  if (index < 0 || index >= (int)slots.size()) return Kind::BANG;
  return slots[index].kind;
}

int gTrigger::SlotInt(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0;
  return slots[index].intValue;
}

float gTrigger::SlotFloat(int index) const {
  if (index < 0 || index >= (int)slots.size()) return 0.f;
  return slots[index].floatValue;
}

std::string gTrigger::SlotText(int index) const {
  if (index < 0 || index >= (int)slots.size()) return std::string();
  if (slots[index].kind != Kind::CONST_SYMBOL) return std::string();
  return slots[index].text;
}

void gTrigger::EmitAll(const Incoming& in, THREAD thread) {
  // **The object.** Right to left, and each Send returns only once the whole
  // subgraph behind that outlet has run — see the header on why that is a
  // guarantee rather than a coincidence. Walking forwards here would be the
  // single bug this object exists to prevent, so it is worth being loud: the
  // loop counts down.
  //
  // Signed index rather than a reverse iterator so the "n-1 down to 0" reads
  // the way the guarantee is stated. No allocation, no lock, no I/O on any
  // branch.
  for (int i = (int)slots.size() - 1; i >= 0; i--) {
    const Slot& slot = slots[(std::size_t)i];
    switch (slot.kind) {
    case Kind::INT:
      // Max: "A symbol, list, or bang received in the inlet will be converted
      // to integer 0 by an i outlet." ExprToInt answers the float inputs C
      // leaves undefined — NaN, infinity, out of range — with 0.
      outputs[(std::size_t)i].SendInt(in.type == Incoming::INT     ? in.intValue
                                      : in.type == Incoming::FLOAT ? ExprToInt(in.floatValue)
                                                                   : 0,
                                      thread);
      break;
    case Kind::FLOAT:
      // "...and to float 0. by an f argument."
      outputs[(std::size_t)i].SendFloat(in.type == Incoming::INT     ? (float)in.intValue
                                        : in.type == Incoming::FLOAT ? in.floatValue
                                                                     : 0.f,
                                        thread);
      break;
    case Kind::BANG:
      // "Anything received in the inlet will be converted to bang before being
      // sent out a b outlet."
      outputs[(std::size_t)i].SendBang(thread);
      break;
    case Kind::LIST:
      // "A list received in the inlet will be sent out unchanged by an l
      // outlet. Anything else will be converted to the single-item list 0."
      outputs[(std::size_t)i].SendList(in.type == Incoming::LIST ? *in.text : zeroList, thread);
      break;
    case Kind::SYMBOL:
      // "A symbol received in the inlet will be sent out unchanged by an s
      // outlet. Anything else will be converted to the null symbol." A list
      // message is the only text this patcher has — see the header.
      outputs[(std::size_t)i].SendList(in.type == Incoming::LIST ? *in.text : nullSymbol, thread);
      break;
    case Kind::CONST_INT:
      outputs[(std::size_t)i].SendInt(slot.intValue, thread);
      break;
    case Kind::CONST_FLOAT:
      outputs[(std::size_t)i].SendFloat(slot.floatValue, thread);
      break;
    case Kind::CONST_SYMBOL:
      outputs[(std::size_t)i].SendList(slot.text, thread);
      break;
    }
  }
}

BANG_IN(SetBang) {
  if (inlet != 0) return;
  Incoming in;
  in.type = Incoming::BANG;
  EmitAll(in, thread);
}

INT_IN(SetInt) {
  if (inlet != 0) return;
  Incoming in;
  in.type = Incoming::INT;
  in.intValue = value;
  EmitAll(in, thread);
}

FLOAT_IN(SetFloat) {
  if (inlet != 0) return;
  Incoming in;
  in.type = Incoming::FLOAT;
  in.floatValue = value;
  EmitAll(in, thread);
}

LIST_IN(SetList) {
  if (inlet != 0) return;
  Incoming in;
  in.type = Incoming::LIST;
  // Borrowed for the duration of the send only: EmitAll never outlives this
  // frame, so nothing here has to own the string.
  in.text = &value;
  EmitAll(in, thread);
}
