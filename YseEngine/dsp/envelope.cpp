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

bool YSE::DSP::envelope::create(YSE::DSP::buffer & source, Int windowSize) {
  // window must be > 0
  if (windowSize <= 0) {
    return false;
  }
  Flt windowDuration = windowSize / 1000.0f;
  Int window = (Int)windowDuration * SAMPLERATE;
  
  Int pos = 0;
  Flt time = 0;
  Int bufferLength = source.getLength();
  Flt * ptr = source.getPtr();
  breakPoints.clear();

  while (pos + window < bufferLength) {
    breakPoints.emplace_back(time, YSE::DSP::getMaxAmplitude(ptr, window));
    ptr += window;
    pos += window;
    time += windowDuration;
  }

  return true;
}

bool YSE::DSP::envelope::create(const char * fileName) {
  breakPoints.clear();
  std::ifstream source;
  source.open(fileName, std::ios_base::in);
  if (source.is_open()) {
    for (std::string line; std::getline(source, line);) {
      std::istringstream in(line);

      // eat whitespace
      in >> std::ws;
      // disregard empty lines
      if (in.eof()) continue;

      // extract values
      float time, value;
      if (in >> time >> value) {
        breakPoints.emplace_back(time, value);
      }
      else {
        breakPoints.clear();
        return false;
      }
    }
  }
  else {
    return false;
  }
  return true;
}

bool YSE::DSP::envelope::saveToFile(const char * fileName) const {
  std::ofstream out;
  out.open(fileName, std::ios::out | std::ios::trunc);
  if (out.is_open()) {
    for (UInt i = 0; i < breakPoints.size(); i++) {
      out << breakPoints[i].time  << "\t" 
          << breakPoints[i].value << "\n";
    }
    out.close();
    return true;
  }
  else {
    return false;
  }
}

UInt YSE::DSP::envelope::elms() const {
  return breakPoints.size();
}

Flt YSE::DSP::envelope::getLengthSec() const {
  if (!elms()) return 0;
  return breakPoints.back().time;
}

const YSE::DSP::envelope::breakPoint & YSE::DSP::envelope::operator[](UInt pos) const {
  return breakPoints[pos];
}

void YSE::DSP::envelope::normalize() {
  // scan for highest value
  Flt max = 0.f;
  breakPoint * ptr = breakPoints.data();
  for (unsigned int i = 0; i < breakPoints.size(); i++) {
    if(max < ptr->value) max = ptr->value;
    ptr++;
  }

  // apply to breakpoints
  if (max != 0.f) {
    Flt multiplier = 1 / max;
    breakPoint * ptr = breakPoints.data();
    for (unsigned int i = 0; i < breakPoints.size(); i++) {
      ptr->value *= multiplier;
      ptr++;
    }
  }
}