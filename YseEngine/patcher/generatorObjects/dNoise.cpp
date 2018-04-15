#include "dNoise.h"

using namespace YSE::PATCHER;

#define className dNoise

CONSTRUCT_DSP()
{
  ADD_OUT_BUFFER;
}

CALC() {
  outputs[0].SendBuffer(&noise(), thread);
}
