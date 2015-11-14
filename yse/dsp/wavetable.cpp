/*
  ==============================================================================

    wavetable.cpp
    Created: 22 Jul 2015 10:51:14am
    Author:  yvan

  ==============================================================================
*/

#include "wavetable.hpp"
#include "../utils/misc.hpp"
#include "sample_functions.hpp"
#include "../internalHeaders.h"


void YSE::DSP::wavetable::createSaw(Int harmonics, Int length) {
  std::vector<Flt> amps;
  for (int i = 0; i < harmonics; i++) {
    amps.emplace_back(1.f / (i + 1));
  }
  createFourierTable(amps, length, -0.25);
}

void YSE::DSP::wavetable::createSquare(Int harmonics, Int length) {
  std::vector<Flt> amps;
  for (int i = 0; i < harmonics; i++) {
    if (i % 2 != 0) amps.emplace_back(1.f / (i + 1));
    else amps.emplace_back(0);
  }
  createFourierTable(amps, length, -0.25);
}

void YSE::DSP::wavetable::createTriangle(Int harmonics, Int length) {
  std::vector<Flt> amps;
  for (int i = 0; i < harmonics; i++) {
    if (i % 2 != 0) amps.emplace_back(1.f / ((i + 1) * (i + 1)));
    else amps.emplace_back(0);
  }
  createFourierTable(amps, length, 0);
}

void YSE::DSP::wavetable::createFourierTable(const std::vector<Flt> & harmonics, Int length, Flt phase) {
  Dbl width;

  if (length + overflow != storage.size()) resize(length + overflow);
  buffer::operator=(0.f);
  phase *= YSE::Pi2;

  for (UInt i = 0; i < harmonics.size(); i++) {
    Flt * ptr = storage.data();
    Flt amplitude = harmonics[i];

    for (int j = 0; j < length; j++) {
      width = (i + 1) * (j * YSE::Pi2 / length);
      *ptr++ += static_cast<Flt>(amplitude * cos(width + phase));
    }

    Normalize(*this);

  }

  copyOverflow();
}

