#include "gAffix.h"
#include "../../implementations/logImplementation.h"
#include "../math/gExprEval.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gAffixBase

namespace {
  // Room for the longest stored message, the separator, and the longest list
  // the patcher's own queues carry (patcherImplementation::kValueListCap, which
  // is the same 256 MESSAGE_CAPACITY is). Anything reaching the object through
  // the patcher therefore joins without the buffer growing at all.
  constexpr std::size_t kOutTextCapacity = (2 * gAffixBase::MESSAGE_CAPACITY) + 1;

  inline bool IsSeparator(char c) {
    return c == ' ' || c == '\t';
  }
} // namespace

gAffixBase::gAffixBase(affixSide affix) : pObject(false), side(affix) {
  ADD_IN_0;
  REG_BANG_IN(PassBang);
  REG_INT_IN(PassInt);
  REG_FLOAT_IN(PassFloat);
  REG_LIST_IN(PassList);

  // The stored message. A list is taken whole (a stored message may be several
  // words); an int and a float are spelled the way a creation argument spells
  // them. Bang is declined rather than swallowed — see the class documentation.
  ADD_IN_1;
  REG_INT_IN(SetStoredInt);
  REG_FLOAT_IN(SetStoredFloat);
  REG_LIST_IN(SetStoredList);

  // ANY rather than LIST: with nothing stored the object is the identity and
  // hands on whatever type arrived.
  ADD_OUT_ANY;

  ADD_PARAM(message);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  // The two allocations the message path would otherwise need, taken here on
  // the control thread for the longest message the object will accept.
  stored.reserve(MESSAGE_CAPACITY);
  outText.reserve(kOutTextCapacity);

  ADD_CATEGORY(pCategory::GENERIC);
}

void gAffixBase::Document(const char* summary, const char* dataDoc, const char* outDoc,
                          const char* paramDoc) {
  ADD_DESCRIPTION(summary);
  INLET_DOC(0, "in", dataDoc, "");
  INLET_DOC(1, "message",
            "Replaces the stored message, without emitting anything. A list is taken whole, an int "
            "or a float as the number it spells. An empty list clears the stored message and makes "
            "the object a pass-through; one longer than 256 characters is refused and the previous "
            "message kept.",
            "any message, at most 256 characters");
  OUTLET_DOC(0, "out", outDoc, "");
  PARAM_DOC("message", "", paramDoc, "any message, at most 256 characters");
}

// A re-parse must not leave half of the previous configuration standing:
// Parameters::Set() calls this before parsing and returns early on an empty
// argument string, so this is the only thing that makes `SetParams("")` a real
// reset — back to the pass-through — rather than a no-op that leaves the object
// joining on its old message.
PARM_CLEAR() {
  message.clear();
  stored.clear();
}

PARM_PARSE() {
  // Control thread only (Parameters::Set), so unlike the inlet path this can
  // afford both a temporary and a reason. The tokens are rejoined with single
  // spaces, which is how the patcher spells a list everywhere else.
  std::string joined;
  for (std::size_t i = 0; i < message.size(); i++) {
    if (i > 0) joined += ' ';
    joined += message[i];
  }

  // Refused rather than truncated, and the object left as a pass-through rather
  // than joining on half a message a downstream .route would not recognise.
  if (joined.size() > MESSAGE_CAPACITY) {
    INTERNAL::LogImpl().emit(E_ERROR, std::string("patcher: ") + Type() +
                                          " message is longer than 256 characters; ignored");
    joined.clear();
  }

  stored.assign(joined);
}

void gAffixBase::StoreText(const char* text, std::size_t length) {
  // Trim both ends: the stored message is joined with a single separator, so
  // leading or trailing whitespace inside it would show up as a second one and
  // give the result an empty token no .route matches.
  std::size_t begin = 0;
  while (begin < length && IsSeparator(text[begin]))
    begin++;
  std::size_t end = length;
  while (end > begin && IsSeparator(text[end - 1]))
    end--;

  const std::size_t size = end - begin;
  // Refused rather than truncated, and silently, since this may be the audio
  // thread. Nothing but whitespace is a real request, not a malformed one: it
  // clears the stored message and returns the object to being a pass-through.
  if (size > MESSAGE_CAPACITY) return;

  stored.assign(text + begin, size);
}

void gAffixBase::Emit(const char* text, std::size_t length, YSE::THREAD thread) {
  // Refilled immediately before the send rather than kept between them: the
  // send path is synchronous, so a patch looping the outlet back into an inlet
  // re-enters here inside the SendList below, and a buffer filled any earlier
  // would be the inner message's by the time this one was read. Into memory
  // reserved at construction, so nothing here allocates for a list up to the
  // 256 characters the patcher's own queues carry.
  if (length == 0) {
    // A bang: there is nothing to join, so the stored message goes out on its
    // own from either side.
    outText.assign(stored);
  } else if (side == affixSide::Front) {
    outText.assign(stored);
    outText.push_back(' ');
    outText.append(text, length);
  } else {
    outText.assign(text, length);
    outText.push_back(' ');
    outText.append(stored);
  }

  outputs[0].SendList(outText, thread);
}

BANG_IN(PassBang) {
  // Max: bang outputs the stored message on its own. With nothing stored there
  // is no message to output, and the identity of a bang is a bang.
  if (stored.empty()) {
    outputs[0].SendBang(thread);
    return;
  }
  Emit("", 0, thread);
}

INT_IN(PassInt) {
  if (stored.empty()) {
    outputs[0].SendInt(value, thread);
    return;
  }
  // ExprFormatValue is the patcher's number-to-text writer: no allocation, no
  // locale (so the separator is always '.', which is what the reader on the
  // other end expects) and no exception.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  Emit(text, (std::size_t)length, thread);
}

FLOAT_IN(PassFloat) {
  if (stored.empty()) {
    outputs[0].SendFloat(value, thread);
    return;
  }
  // A float keeps its decimal point, so it stays visibly a float in the middle
  // of the list it has just become.
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  Emit(text, (std::size_t)length, thread);
}

LIST_IN(PassList) {
  if (stored.empty()) {
    // Handed on unchanged rather than copied through the join buffer: with
    // nothing stored there is nothing to join, and the object is a wire.
    outputs[0].SendList(value, thread);
    return;
  }
  Emit(value.c_str(), value.size(), thread);
}

LIST_IN(SetStoredList) {
  StoreText(value.c_str(), value.size());
}

INT_IN(SetStoredInt) {
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Int(value), text, kExprValueTextMax);
  StoreText(text, (std::size_t)length);
}

FLOAT_IN(SetStoredFloat) {
  char text[kExprValueTextMax];
  const int length = ExprFormatValue(ExprValue::Float(value), text, kExprValueTextMax);
  StoreText(text, (std::size_t)length);
}

#undef className

// ─── the two insertion points ─────────────────────────────────────────────────

gPrepend::gPrepend() : gAffixBase(affixSide::Front) {
  Document("Puts a stored message in front of every message that arrives, and sends the two out "
           "joined as a list. The patcher's routing family reads the first token of a list — "
           ".route and .routepass match on it, .sel compares against it, and every object with a "
           "message interface is addressed by it — so this is how a patch builds a message with a "
           "leading selector out of a value it computed. The stored message comes from the "
           "creation argument or from inlet 1; with none stored the object passes what it is given "
           "straight through, unchanged and as the type it arrived as.",
           "Bang emits the stored message on its own; an int, float or list is emitted with the "
           "stored message in front of it. With nothing stored the value is passed through as the "
           "type it arrived as.",
           "The stored message followed by what arrived, as a list — or what arrived unchanged "
           "when nothing is stored.",
           "Message to put in front of every input — the whole argument string, so \"note 60\" is "
           "two words. Empty makes the object a pass-through.");
}

gAppend::gAppend() : gAffixBase(affixSide::Back) {
  Document("Puts a stored message after every message that arrives, and sends the two out joined "
           "as a list. The mirror of .prepend: where that one builds the selector a message starts "
           "with, this one supplies the trailing arguments it ends with — a fixed channel number, "
           "a target index, the second half of a pair whose first half is computed. The stored "
           "message comes from the creation argument or from inlet 1; with none stored the object "
           "passes what it is given straight through, unchanged and as the type it arrived as.",
           "Bang emits the stored message on its own; an int, float or list is emitted with the "
           "stored message after it. With nothing stored the value is passed through as the type "
           "it arrived as.",
           "What arrived followed by the stored message, as a list — or what arrived unchanged "
           "when nothing is stored.",
           "Message to put after every input — the whole argument string, so \"note 60\" is two "
           "words. Empty makes the object a pass-through.");
}
