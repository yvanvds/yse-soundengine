#include "pDac.h"
#include "../pListArgs.h"
#include <string>

using namespace YSE::PATCHER;

#define className pDac

namespace {
  // Inlet count without arguments on the registry / metadata construction
  // path (pDac::Create()). The live graph always builds the DAC with the
  // patcher's real output channel count through the pDac(int) constructor
  // (patcherImplementation::NewObjectUnlocked), so this default only shapes
  // the documentation snapshot, never a rendered graph.
  constexpr int kDefaultDacChannels = 2;
} // namespace

pDac::pDac() : pDac(kDefaultDacChannels) {}

pDac::pDac(int channelCount) : pAudioIO(channelCount) {
  REG_PARM_CLEAR;
  REG_PARM_PARSE;
  ADD_PARAM(creationArgs);
  ShapePorts();

  ADD_DESCRIPTION("The patch's audio output. Each inlet goes to one output channel, passed on to "
                  "the sound or insert that hosts the patcher. Also turns the patcher's audio on "
                  "and off.");
  ADD_CATEGORY(pCategory::IO);
  PARAM_DOC("channels", "",
            "The output channel of each inlet, from 1, one inlet per number. Without them, one "
            "inlet per patcher channel. A channel the patcher does not have is silent.",
            "1 to 1024, one or more");
}

void pDac::ShapePorts() {
  std::vector<int> routing;
  ArgumentChannels(routing, false);
  BuildRoutes(routing);

  // Rebuilt rather than resized: the inlet count comes from the arguments.
  // Every caller runs before the object is wired or published.
  inputs.clear();
  channels.assign(routing.size(), nullptr);
  for (std::size_t i = 0; i < routing.size(); i++) {
    inputs.emplace_back(this, false, static_cast<int>(i));
    REG_BUFFER_IN(SetBuffer);
    REG_INT_IN(SetSwitchInt);
    REG_FLOAT_IN(SetSwitchFloat);
    REG_LIST_IN(SetList);
    inputs.back().SetDoc(
        "in",
        "One channel of the patch's audio output. A number turns the patcher's audio off (0) "
        "or on; also 'start', 'stop', 'set <channel>' for this inlet, or a list of channels "
        "for the inlets from the first.",
        "-1.0 to 1.0");
  }
}

PARM_CLEAR() {
  creationArgs.clear();
  ShapePorts();
}

PARM_PARSE() {
  ShapePorts();
}

BUFFER_IN(SetBuffer) {
  (void)thread;
  channels[static_cast<std::size_t>(inlet)] = buffer;
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

// Word-first text arrives here too: the object has no command channel, so
// the inlet hands `start`, `set 3` and the like to the list handler with the
// inlet it came in on (inlet::SetMessage). Walked in place, allocation-free.
LIST_IN(SetList) {
  (void)thread;
  if (HandleSwitch(value)) return;
  std::size_t begin = 0;
  std::size_t end = 0;
  if (!NextToken(value, 0, begin, end)) return;
  if (TokenIs(value, begin, end, "set")) {
    SetRoutesFrom(static_cast<unsigned int>(inlet), value, end);
    return;
  }
  float number = 0.f;
  if (ReadNumericToken(value.c_str() + begin, end - begin, number)) SetRoutesFrom(0, value, 0);
}

RESET() // {
for (unsigned int i = 0; i < channels.size(); i++) {
  channels[i] = nullptr;
}
}

YSE::DSP::buffer* pDac::GetBuffer(unsigned int inlet) {
  if (inlet < channels.size()) {
    return channels[inlet];
  }
  return nullptr;
}

#undef className

pEzdac::pEzdac() : pDac() {
  Describe();
}

pEzdac::pEzdac(int channelCount) : pDac(channelCount) {
  Describe();
}

void pEzdac::Describe() {
  ADD_DESCRIPTION("The patch's audio output as an on/off button: ~dac, plus a GUI value that shows "
                  "whether the patcher's audio is on.");
}

std::string pEzdac::GetGuiValue() {
  return DspOn() ? "on" : "off";
}
