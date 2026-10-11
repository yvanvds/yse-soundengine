#include "dTapout.h"
#include "dTapin.h"
#include "../pListArgs.h"
#include "../pSelector.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className dTapout

CONSTRUCT_DSP(), silence(YSE::STANDARD_BUFFERSIZE) {
  silence = 0.f;
  for (aFlt& d : delays)
    d = 0.f;

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(creationArgs);

  ShapePorts();

  ADD_DESCRIPTION("Reads the delay line of a ~tapin at one or more delay times. One tap per "
                  "argument, each with its own delay inlet and output. Feedback from an output "
                  "back into the ~tapin is allowed.");
  ADD_CATEGORY(pCategory::DELAY);
  PARAM_DOC("delays", "0",
            "The starting delay of each tap in ms, one argument per tap (at most 64). The "
            "number of arguments is the number of taps.",
            "one or more ms values");
}

void dTapout::ShapePorts() {
  // Rebuilt rather than resized: the tap count comes from the arguments, so
  // the pins and the taps have to agree. Every caller runs before the object
  // is wired or published.
  int count = 0;
  for (const std::string& token : creationArgs) {
    float number = 0.f;
    if (token.empty() || !ReadNumericToken(token, number)) continue;
    if (count >= MAX_TAPS) break;
    delays[static_cast<std::size_t>(count)] = number;
    count++;
  }
  if (count == 0) {
    delays[0] = 0.f;
    count = 1;
  }

  inputs.clear();
  outputs.clear();
  taps.clear();
  taps.resize(static_cast<std::size_t>(count));

  for (int i = 0; i < count; i++) {
    if (i == 0) {
      ADD_IN_0;
      REG_LINK_IN;
      REG_LIST_IN(SetDelays);
    } else {
      inputs.emplace_back(this, false, i);
    }
    REG_SIGNAL_IN;
    REG_FLOAT_IN(SetDelay);
    REG_INT_IN(SetDelayInt);
    ADD_OUT_BUFFER;

    const std::string n = std::to_string(i + 1);
    inputs.back().SetDoc(
        i == 0 ? "line, delay 1" : "delay " + n,
        (i == 0 ? std::string("The link from ~tapin's outlet, and the delay of tap 1. ")
                : std::string()) +
            "Delay of tap " + n +
            " in ms: a signal (interpolated) or a number (a fixed delay used while no signal is "
            "connected)." +
            (i == 0 ? std::string(" A list sets the fixed delays of several taps.")
                    : std::string()),
        "one block - the ~tapin's maximum");
    outputs.back().SetDoc("tap " + n, "The line delayed by tap " + n + "'s delay time.",
                          "any signal");
  }
}

PARM_CLEAR() {
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

FLOAT_IN(SetDelay) {
  (void)thread;
  if (inlet >= 0 && inlet < MAX_TAPS) delays[static_cast<std::size_t>(inlet)] = value;
}

INT_IN(SetDelayInt) {
  (void)thread;
  if (inlet >= 0 && inlet < MAX_TAPS)
    delays[static_cast<std::size_t>(inlet)] = static_cast<float>(value);
}

// A list sets the fixed delays from this tap on, one number each. Walked in
// place so nothing allocates on whichever thread delivers it.
LIST_IN(SetDelays) {
  (void)thread;
  const char* text = value.c_str();
  const std::size_t length = value.size();
  const int count = TapCount();
  int tap = inlet;
  std::size_t pos = 0;
  while (pos < length && tap < count) {
    while (pos < length && IsSelectorSeparator(text[pos]))
      pos++;
    std::size_t end = pos;
    while (end < length && !IsSelectorSeparator(text[end]))
      end++;
    if (end == pos) break;
    float number = 0.f;
    if (ReadNumericToken(text + pos, end - pos, number)) {
      delays[static_cast<std::size_t>(tap)] = number;
    }
    tap++;
    pos = end;
  }
}

CALC() {
  // The ~tapin at the other end of the link, from the pinned snapshot. Only a
  // ~tapin's link outlet can end on inlet 0, but the type is checked anyway
  // rather than trusted.
  const pObject* source = inputs[0].LinkedObject();
  const dTapin* tapin = (source != nullptr && std::strcmp(source->Type(), YSE::OBJ::D_TAPIN) == 0)
                            ? static_cast<const dTapin*>(source)
                            : nullptr;
  const int count = TapCount();
  for (int i = 0; i < count; i++) {
    const auto at = static_cast<std::size_t>(i);
    if (tapin == nullptr) {
      outputs[at].SendBuffer(&silence, thread);
      continue;
    }
    // A connected delay signal wins over the number (issue #1036).
    DSP::buffer& out = inputs[at].IsConstant()
                           ? taps[at](tapin->Line(), delays[at])
                           : taps[at](tapin->Line(), inputs[at].Signal(delays[at]));
    outputs[at].SendBuffer(&out, thread);
  }
}

#undef className
