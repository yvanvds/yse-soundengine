#include "pAdc.h"
#include <string>

using namespace YSE::PATCHER;

#define className pAdc

namespace {
  // Channel count used by the registry / metadata construction path
  // (pAdc::Create()). The live graph always builds the ADC with the patcher's
  // real output channel count through the pAdc(int) constructor
  // (patcherImplementation::CreateObjectUnlocked), so this default only shapes
  // the documentation snapshot, never a rendered graph.
  constexpr int kDefaultAdcChannels = 2;
} // namespace

CONSTRUCT_DSP() {
  build(kDefaultAdcChannels);
}

pAdc::pAdc(int channels) : pObject(true) {
  build(channels);
}

CALC() {
  for (unsigned int i = 0; i < channels.size(); i++) {
    if (channels[i] != nullptr) {
      outputs[i].SendBuffer(channels[i], thread);
    }
  }
}

void pAdc::SetChannelBuffer(unsigned int channel, YSE::DSP::buffer* buffer) {
  if (channel < channels.size()) {
    channels[channel] = buffer;
  }
}

unsigned int pAdc::NumChannels() const {
  return static_cast<unsigned int>(channels.size());
}

void pAdc::build(int chans) {
  channels.assign(static_cast<size_t>(chans < 0 ? 0 : chans), nullptr);
  for (int i = 0; i < chans; i++) {
    ADD_OUT_BUFFER;
    OUTLET_DOC(i, "out", "One channel of the host's incoming audio, injected into the graph.",
               "-1.0 to 1.0");
  }
  ADD_DESCRIPTION("Audio input into a patcher graph. When the patcher runs as a channel or sound "
                  "insert, each outlet carries one channel of the host's incoming audio, so the "
                  "graph can process external audio rather than only generating it.");
  ADD_CATEGORY(pCategory::GENERIC);
}
