#include "pDac.h"
#include "../../implementations/logImplementation.h"
#include <string>

using namespace YSE::PATCHER;

#define className pDac

namespace {
  // Channel count used by the registry / metadata construction path
  // (pDac::Create()). The live graph always builds the DAC with the patcher's
  // real output channel count through the pDac(int) constructor
  // (patcherImplementation::CreateObjectUnlocked), so this default only shapes
  // the documentation snapshot, never a rendered graph.
  constexpr int kDefaultDacChannels = 2;
} // namespace

CONSTRUCT_DSP() {
  build(kDefaultDacChannels);
}

pDac::pDac(int channels) : pObject(true) {
  build(channels);
}

void pDac::build(int chans) {
  channels.assign(static_cast<size_t>(chans < 0 ? 0 : chans), nullptr);
  for (int i = 0; i < chans; i++) {
    inputs.emplace_back(this, false, i);
    inputs.back().RegisterBuffer(std::bind(&pDac::SetBuffer, this, std::placeholders::_1,
                                           std::placeholders::_2, std::placeholders::_3));
    INLET_DOC(i, "in", "One channel of the graph's audio output, handed to the patcher's host.",
              "-1.0 to 1.0");
  }
  ADD_DESCRIPTION("Audio output of a patcher graph. Each inlet takes one channel of audio and the "
                  "patcher passes it on to whatever hosts it — a channel, a sound, or the engine's "
                  "output. A patch is only audible once something reaches this object.");
  ADD_CATEGORY(pCategory::GENERIC);
}

BUFFER_IN(SetBuffer) {
  channels[inlet] = buffer;
}

RESET() // {
for (unsigned int i = 0; i < channels.size(); i++) {
  channels[i] = nullptr;
}
}

YSE::DSP::buffer* pDac::GetBuffer(unsigned int output) {
  if (output < channels.size()) {
    return channels[output];
  }
  return nullptr;
}
