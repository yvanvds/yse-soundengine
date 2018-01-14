#include "pLine.h"
#include "..\pObjectList.hpp"
#include <cstring>

using namespace YSE::PATCHER;

pObject * pLine::Create() { return new pLine(); }

pLine::pLine() {
  // in 0: target value (float)
  // in 1: time in milliseconds (int)

  // out 0: dsp buffer

  inputs.emplace_back(PIN_TYPE::PIN_FLOAT | PIN_TYPE::PIN_STRING, 0, this);
  inputs.emplace_back(PIN_TYPE::PIN_INT, 1, this);
  outputs.emplace_back(PIN_TYPE::PIN_DSP_BUFFER, 0, this);

  inputs[0].SetData(1.f);
  inputs[1].SetData(0);
}

const char * pLine::Type() const {
  return YSE::OBJ::LINE;
}

void pLine::RequestData() {
  UpdateInputs();

  if (inputs[0].GetCurentDataType() == PIN_STRING) {
    if (strcmp(inputs[0].GetString(), "stop") == 0) {
      ramp.stop();
    }
    else {
      float target = 1.f;
      int time = 0;
      if (inputs[0].GetCurentDataType() == PIN_FLOAT) {
        target = inputs[0].GetFloat();
      }
      if (inputs[1].GetCurentDataType() == PIN_INT) {
        time = inputs[1].GetInt();
      }
      ramp.setIfNew(target, time);
    }
  }

  ramp.update();
  outputs[0].SetData(&ramp());
}