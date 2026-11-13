#include "pAdc.h"
#include "../pListArgs.h"
#include <string>

using namespace YSE::PATCHER;

#define className pAdc

namespace {
  // Outlet count without arguments on the registry / metadata construction
  // path (pAdc::Create()). The live graph always builds the ADC with the
  // patcher's real output channel count through the pAdc(int) constructor
  // (patcherImplementation::NewObjectUnlocked), so this default only shapes
  // the documentation snapshot, never a rendered graph.
  constexpr int kDefaultAdcChannels = 2;
} // namespace

pAdc::pAdc() : pAdc(kDefaultAdcChannels) {}

pAdc::pAdc(int channelCount) : pAudioIO(channelCount) {
  // in 0: messages only. Not hot, and not a signal inlet, so the object
  // stays a DSP start point and a message never computes it early.
  inputs.emplace_back(this, false, 0);
  REG_INT_IN(SetSwitchInt);
  REG_FLOAT_IN(SetSwitchFloat);
  REG_LIST_IN(SetList);

  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(creationArgs);
  ShapePorts();

  ADD_DESCRIPTION("The patch's audio input. When the patcher runs as an insert effect, each outlet "
                  "carries one channel of the audio it processes. Also turns the patcher's audio "
                  "on and off.");
  ADD_CATEGORY(pCategory::IO);
  INLET_DOC(0, "control",
            "A number turns the patcher's audio off (0) or on; also 'start' and 'stop'. "
            "'set <outlet> <channel>' routes one outlet, a list of channels the outlets from the "
            "first.",
            "0 or 1, or a message");
  PARAM_DOC("channels", "",
            "The input channel of each outlet, from 1, one outlet per number; one number N alone "
            "is channels 1 to N. Without them, one outlet per patcher channel.",
            "1 to 1024, one or more");
}

void pAdc::ShapePorts() {
  std::vector<int> routing;
  ArgumentChannels(routing, true);
  BuildRoutes(routing);

  // Rebuilt rather than resized: the outlet count comes from the arguments.
  // Every caller runs before the object is wired or published.
  outputs.clear();
  channels.assign(routing.size(), nullptr);
  for (std::size_t i = 0; i < routing.size(); i++) {
    ADD_OUT_BUFFER;
    outputs.back().SetDoc("out", "One channel of the host's incoming audio.", "-1.0 to 1.0");
  }
}

PARM_CLEAR() {
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

CALC() {
  for (unsigned int i = 0; i < channels.size(); i++) {
    if (channels[i] != nullptr) {
      outputs[i].SendBuffer(channels[i], thread);
    }
  }
}

INT_IN(SetSwitchInt) {
  (void)inlet;
  (void)thread;
  HandleSwitchNumber(static_cast<float>(value));
}

FLOAT_IN(SetSwitchFloat) {
  (void)inlet;
  (void)thread;
  HandleSwitchNumber(value);
}

// Word-first text arrives here too (the object has no command channel).
// Walked in place, allocation-free.
LIST_IN(SetList) {
  (void)inlet;
  (void)thread;
  if (HandleSwitch(value)) return;
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  float number = 0.f;
  if (TokenIs(value, begin, end, "set")) {
    // set <outlet> <channel>, both from 1.
    std::size_t at = 0;
    if (!NextToken(value, end, at, end)) return;
    if (!ReadNumericToken(value.c_str() + at, end - at, number)) return;
    if (!(number >= 1.f) || !(number <= static_cast<float>(MAX_PINS))) return;
    SetRoutesFrom(static_cast<unsigned int>(number) - 1, value, end);
    return;
  }
  if (ReadNumericToken(value.c_str() + begin, end - begin, number)) SetRoutesFrom(0, value, 0);
}

void pAdc::SetChannelBuffer(unsigned int outlet, YSE::DSP::buffer* buffer) {
  if (outlet < channels.size()) {
    channels[outlet] = buffer;
  }
}

unsigned int pAdc::NumChannels() const {
  return static_cast<unsigned int>(channels.size());
}

#undef className

pEzadc::pEzadc() : pAdc() {
  Describe();
}

pEzadc::pEzadc(int channelCount) : pAdc(channelCount) {
  Describe();
}

void pEzadc::Describe() {
  ADD_DESCRIPTION("The patch's audio input as an on/off button: ~adc, plus a GUI value that shows "
                  "whether the patcher's audio is on.");
}

std::string pEzadc::GetGuiValue() {
  return DspOn() ? "on" : "off";
}
