#include "pLine.h"
#include <cstring>

using namespace YSE::PATCHER;

#define className pLine

CONSTRUCT_DSP() {
  // in 0: target value (float)
  // in 1: time in milliseconds (int)

  // out 0: dsp buffer

  ADD_IN_0;
  REG_FLOAT_IN(SetTarget);

  ADD_IN_1;
  REG_FLOAT_IN(SetTime);

  ADD_OUT_BUFFER;

  // std::atomic's default constructor leaves the value indeterminate before
  // C++20, so both have to be set here: a fresh ~line used to ramp toward
  // whatever the heap held, and render that at full scale until its first
  // message arrived (issue #951). 0 is what PARAM_DOC documents.
  target = 0.f;
  time = 0.f;
  ADD_PARAM(target);
  ADD_PARAM(time);

  stop = false;

  ADD_DESCRIPTION("Linear-ramp generator. Ramps an audio-rate value toward a target over a given "
                  "time in ms.");
  ADD_CATEGORY(pCategory::OSC);
  INLET_DOC(0, "target", "Target value to ramp toward. Also takes 'stop'.", "any float");
  INLET_DOC(1, "time", "Ramp duration in milliseconds, used from the next new target.", "0+ ms");
  OUTLET_DOC(0, "out", "Audio-rate ramp output.", "any float");
  PARAM_DOC("target", "0", "Initial target value.", "any float");
  PARAM_DOC("time", "0", "Initial ramp time in milliseconds.", "0+ ms");
}

MESSAGES() {
  if (message == "stop") {
    stop = true;
  }
}

FLOAT_IN(SetTarget) {
  target = value;
}

FLOAT_IN(SetTime) {
  time = value;
}

CALC() {
  if (stop) {
    // Holds for this block only: `target` still has the old value, so the next
    // setIfNew() restarts the ramp (#1019).
    ramp.stop();
    stop = false;
  } else {
    ramp.setIfNew(target, (int)time);
  }
  ramp.update();
  outputs[0].SendBuffer(&ramp(), thread);
}