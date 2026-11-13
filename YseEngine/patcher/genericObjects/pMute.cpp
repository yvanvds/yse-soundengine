// `~mute` (issue #1137): see pMute.h. The mute itself lives on the subpatcher
// and is applied by the render (patcherImplementation::Calculate).
#include "pMute.h"

#include "gSubpatcher.h"
#include "../pListArgs.h"
#include "../pSelector.h"

using namespace YSE::PATCHER;

#define className pMute

namespace {

  // The next number in `text` from `cursor`, skipping separators. False when
  // the next token is missing or not a number. No allocation.
  bool NextNumber(const std::string& text, std::size_t& cursor, float& out) {
    const std::size_t length = text.size();
    std::size_t begin = cursor;
    while (begin < length && IsSelectorSeparator(text[begin]))
      begin++;
    std::size_t end = begin;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    cursor = end;
    return end > begin && ReadNumericToken(text.c_str() + begin, end - begin, out);
  }

} // namespace

CONSTRUCT_DSP() {
  ADD_IN_0;
  REG_INT_IN(SetInt);
  REG_FLOAT_IN(SetFloat);
  REG_LIST_IN(SetList);

  // A control outlet: what it carries is the mute, applied to the subpatcher
  // behind the cord, never a message.
  ADD_OUT_ANY;

  ADD_DESCRIPTION("Switches signal processing in subpatchers off and on. Connect the outlet to any "
                  "inlet of a subpatcher: 1 mutes the signal objects inside it, 0 unmutes them.");
  ADD_CATEGORY(pCategory::ENCAPSULATION);

  INLET_DOC(0, "mute",
            "A number: non-zero mutes, 0 unmutes. A list <mute> <nested>: with nested non-zero, "
            "the subpatchers nested inside are muted or unmuted too.",
            "0 or 1");
  OUTLET_DOC(0, "to subpatcher",
             "Connect to any inlet of a subpatcher to control it. Carries no messages: nothing "
             "arrives at the subpatcher's inlet.",
             "");
}

void pMute::Apply(bool mute, bool nested) {
  const std::uint8_t mask =
      nested ? (gSubpatcher::MUTE_CONTENTS | gSubpatcher::MUTE_NESTED) : gSubpatcher::MUTE_CONTENTS;
  outputs[0].SendSubpatchMute(mask, mute);
}

INT_IN(SetInt) {
  (void)inlet;
  (void)thread;
  Apply(value != 0, false);
}

FLOAT_IN(SetFloat) {
  (void)inlet;
  (void)thread;
  Apply(value != 0.f, false);
}

LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  std::size_t cursor = 0;
  float mute = 0.f;
  if (!NextNumber(value, cursor, mute)) return;
  float nested = 0.f;
  const bool hasNested = NextNumber(value, cursor, nested);
  Apply(mute != 0.f, hasNested && nested != 0.f);
}

#undef className
