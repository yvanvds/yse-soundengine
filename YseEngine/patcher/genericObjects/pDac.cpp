#include "pDac.h"

using namespace YSE::PATCHER;

#define className pDac

CONSTRUCT_DSP() {} // should not be used

pDac::pDac(int channels )
  : pObject(true) {

  for (int i = 0; i < channels; i++) {
    inputs.emplace_back(this, false, i);
    inputs.back().RegisterBuffer(std::bind(&pDac::SetBuffer, this, std::placeholders::_1, std::placeholders::_2));
    this->channels.resize(channels);
  }
}

BUFFER_IN(pDac::SetBuffer) {
  channels[inlet] = buffer;
}

RESET() // {
  for (int i = 0; i < channels.size(); i++) {
    channels[i] = nullptr;
  }
}

YSE::DSP::buffer * pDac::GetBuffer(int output) {
  if (output < channels.size()) {
    return channels[output];
  }
  return nullptr;
}
