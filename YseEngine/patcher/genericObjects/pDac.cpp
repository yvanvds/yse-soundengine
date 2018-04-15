#include "pDac.h"

using namespace YSE::PATCHER;

#define className pDac

CONSTRUCT_DSP() {} // should not be used

pDac::pDac(int channels )
  : pObject(true) {

  for (int i = 0; i < channels; i++) {
    inputs.emplace_back(this, false, i);
    inputs.back().RegisterBuffer(std::bind(&pDac::SetBuffer, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    this->channels.resize(channels);
  }
}

BUFFER_IN(SetBuffer) {
  channels[inlet] = buffer;
}

RESET() // {
  for (unsigned int i = 0; i < channels.size(); i++) {
    channels[i] = nullptr;
  }
}

YSE::DSP::buffer * pDac::GetBuffer(unsigned int output) {
  if (output < channels.size()) {
    return channels[output];
  }
  return nullptr;
}
