/*
  ==============================================================================

    envelope.cpp
    Created: 18 Jul 2015 6:54:05pm
    Author:  yvan

  ==============================================================================
*/

#include <fstream>

#include "envelope.hpp"
#include "../internalHeaders.h"

bool YSE::DSP::envelope::create(YSE::DSP::sample & source, Int windowSize) {
  // window must be > 0
  if (windowSize <= 0) {
    return false;
  }
  Flt windowDuration = windowSize / 1000.0;
  Int window = windowDuration * SAMPLERATE;
  
  Int pos = 0;
  Flt time = 0;
  Int bufferLength = source.getLength();
  Flt * ptr = source.getBuffer();
  breakPoints.clear();

  while (pos + window < bufferLength) {
    breakPoints.emplace_back(time, YSE::DSP::getMaxAmplitude(ptr, window));
    ptr += window;
    pos += window;
    time += windowDuration;
  }

  return true;
}

bool YSE::DSP::envelope::toFile(const char * fileName) {
  std::ofstream out;
  out.open(fileName, std::ios::out | std::ios::trunc);
  if (out.is_open()) {
    for (UInt i = 0; i < breakPoints.size(); i++) {
      out << breakPoints[i].time << "\t" << breakPoints[i].value << "\n";
    }
    out.close();
    return true;
  }
  else {
    return false;
  }
}