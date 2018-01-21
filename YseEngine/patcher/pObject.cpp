#include "pObject.h"
#include "pHandle.hpp"
#include "headers\enums.hpp"

using namespace YSE::PATCHER;

pObject::pObject(bool isDSPObject) 
  :DSP(isDSPObject)
{}

bool pObject::IsDSPStartPoint() {
  if(!DSP) return false;

  for (unsigned int i = 0; i < inputs.size(); i++) {
    if (inputs[i].HasActiveDSPConnection()) return false;
  }

  // at this point either the dsp pin is not connected or there is none
  return true;
}

void pObject::ResetDSP() {
  for (unsigned int i = 0; i < inputs.size(); i++) {
    inputs[i].ResetDSP();
  }
}

void pObject::CalculateIfReady() {
  // make sure all dsp inputs are ready
  for (unsigned int i = 0; i < inputs.size(); i++) {
    if (inputs[i].WaitingForDSP()) {
      return;
    }
  }

  Calculate();
}


void pObject::ConnectInlet(outlet * from, int inlet)
{
  inputs[inlet].Connect(from);
}

void pObject::DisconnectInlet(outlet * from, int inlet) {
  inputs[inlet].Disconnect(from);
}

void pObject::ConnectOutlet(inlet * dest, int outlet) {
  outputs[outlet].Connect(dest);
}

YSE::OUT_TYPE pObject::GetOutputType(unsigned int output) const {
  if (output < 0 || output >= outputs.size()) return OUT_TYPE::INVALID;

  return outputs[output].Type();
}


YSE::PATCHER::inlet * pObject::GetInlet(int number) {
  return &(inputs[number]);
}

YSE::PATCHER::outlet * pObject::GetOutlet(int number) {
  return &(outputs[number]);
}

