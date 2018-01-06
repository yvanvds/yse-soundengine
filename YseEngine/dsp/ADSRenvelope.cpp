/*
  ==============================================================================

    ADSRenvelope.cpp
    Created: 26 Jul 2015 3:25:02pm
    Author:  yvan

  ==============================================================================
*/

#include "ADSRenvelope.hpp"
#include "sample_functions.hpp"
#include <cmath>

void YSE::DSP::ADSRenvelope::addPoint(const breakPoint & point) {
  breakPoints.emplace_back(point);
}

void YSE::DSP::ADSRenvelope::generate() {
  if (breakPoints.size() < 2) return;
  loopStart = loopEnd = nullptr;

  envelope.resize((unsigned int)(breakPoints.back().time * SAMPLERATE));

  Flt * ptr = envelope.getPtr();
  UInt currentPoint = 0;
  UInt lastTarget = 0;
  UInt target = (unsigned int)(breakPoints[1].time * SAMPLERATE);

  for (unsigned int i = 0; i < envelope.getLength(); i++) {
    *ptr++ = breakPoints[currentPoint].value
      + (breakPoints[currentPoint + 1].value - breakPoints[currentPoint].value)
      * std::pow(
            (i - lastTarget) / static_cast<double>(target - lastTarget)
            , breakPoints[currentPoint].coef
        );

    if (i == target) {
      lastTarget = target;
      currentPoint++;
      target = (unsigned int)(breakPoints[currentPoint + 1].time * SAMPLERATE);
      if (breakPoints[currentPoint].loopStart) loopStart = ptr;
      if (breakPoints[currentPoint].loopEnd) loopEnd = ptr;
    }
  }
  envelopeEnd = ptr;
}

void YSE::DSP::ADSRenvelope::saveToFile(const char * fileName) {
  SaveToFile(fileName, envelope);
}

YSE::DSP::buffer & YSE::DSP::ADSRenvelope::operator()(STATE state, UInt length) {
  if (length != result.getLength()) result.resize(length);

  // envelope should start from the beginning on a new note
  if (state == ADSRenvelope::ATTACK) {
    phase = envelope.getPtr();
    if (loopEnd != nullptr && loopStart != nullptr) looping = true;
    else looping = false;
    endReached = false;
  }

  // when called after release has reached zero
  if (endReached) {
    result = 0.f;
    return result;
  }

  // note is released, so go to the loop end (release moment) but
  // take into account that the current amplitude might be different
  else if (state == ADSRenvelope::RELEASE && loopEnd != nullptr) {
    // find the point nearest to the loop end with the same
    // value as the current point as to avoid a glitch when changing phase
    Flt * search = loopEnd;
    while (*phase != *search) search--;
    phase = search;
    looping = false;
  }


  // fill the result buffer
  Flt * out = result.getPtr();
  Int i = length;
  while (i--) {
    *out++ = *phase++;
    if (looping && phase == loopEnd) phase = loopStart;
    if (phase == envelopeEnd) {
      endReached = true;
      // fill rest with zeroes
      if (i) {
        while (i--) *out++ = 0.f;
      }
      break;
    }
  }

  
  return result;
}