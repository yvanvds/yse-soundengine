/*
  ==============================================================================

    lfo.cpp
    Created: 18 Sep 2015 6:10:24pm
    Author:  yvan

  ==============================================================================
*/

#include "lfo.hpp"
#include "../utils/misc.hpp"
#include "oscillators.hpp"

// static lookup waves
YSE::DSP::fileBuffer LfoSawTable(0);
YSE::DSP::fileBuffer LfoTriangleTable(0);
YSE::DSP::fileBuffer LfoSineTable(0);

YSE::DSP::lfo::lfo() : cursor(0.f), previousType(LFO_NONE) {
  result = 1;
  if (LfoSawTable.getLength() == 0) {
    // This is the first lfo object in use.
    // Initialize all lookup tables now.
    LfoSawTable.resize(SAMPLERATE);
    LfoSawTable.drawLine(0, SAMPLERATE - 200, 1.f, 0.f);
    LfoSawTable.drawLine(SAMPLERATE - 200, SAMPLERATE, 0.f, 1.f);

    LfoTriangleTable.resize(SAMPLERATE);
    LfoTriangleTable.drawLine(0, (UInt)(SAMPLERATE * 0.5f), 0.f, 1.f);
    LfoTriangleTable.drawLine((UInt)(SAMPLERATE * 0.5f), SAMPLERATE, 1.f, 0.f);

    LfoSineTable.resize(SAMPLERATE);
    // perhaps this is a bit clumsy...
    sine s;
    for (UInt i = 0; i < SAMPLERATE; i += STANDARD_BUFFERSIZE) {
      LfoSineTable.copyFrom(s(1), 0, i, STANDARD_BUFFERSIZE);
    }
    LfoSineTable += 1;
    LfoSineTable *= 0.5;
  }
}

YSE::DSP::buffer & YSE::DSP::lfo::operator()(LFO_TYPE type, Flt frequency) {
  // avoid divisions by zero
  if (frequency == 0) type = LFO_NONE;
  
  switch (type) {
    case LFO_NONE: {
      result = 1;
      previousType = type;
      break;
    }

    case LFO_RANDOM:
    case LFO_SQUARE: {
      // shorten current value if new frequency is lower
      UInt phaseLength = (UInt)(SAMPLERATE / frequency * 0.5f);
      if (phaseLength < lineLength) lineLength = phaseLength;

      if (previousType != type) {
        lineLength = phaseLength;
        currentLineValue = type == LFO_SQUARE ? 1.f : RandomF();
        previousType = type;
      }

      if (lineLength > result.getLength()) {
        lineLength -= result.getLength();
        result = currentLineValue;
      } else {
        UInt samplesToProcess = result.getLength();
        UInt pos = 0;
        while (samplesToProcess) {
          UInt steps = lineLength > samplesToProcess ? samplesToProcess : lineLength;
          if (previousLineValue != currentLineValue) {
            UInt rampSteps = steps > 200 ? 200 : steps;
            result.drawLine(pos, pos + rampSteps, previousLineValue, currentLineValue);
            result.drawLine(pos + rampSteps, pos + steps, currentLineValue);
            previousLineValue = currentLineValue;
          }
          else {
            result.drawLine(pos, pos + steps, currentLineValue);
          }
          
          lineLength -= steps;
          
          if (!lineLength) {
            previousLineValue = currentLineValue;
            if (type == LFO_SQUARE) currentLineValue = currentLineValue > 0.9f ? 0.f : 1.f;
            else {
              // type = LFO_RANDOM
              currentLineValue = RandomF();
            }
            lineLength = phaseLength;
          }
          
          pos += steps;
          samplesToProcess -= steps;
        }   
      }
      break;
    }

    case LFO_SAW: {
      previousType = LFO_SAW;
      UInt samplesToProcess = result.getLength();
      Flt * out = result.getPtr();
      Flt * in  = LfoSawTable.getPtr();
      while (samplesToProcess) {
        *out++ = in[(UInt)cursor];
        cursor += frequency;
        if (cursor >= SAMPLERATE) cursor -= SAMPLERATE;
        samplesToProcess--;
      }
      break;
    }

    case LFO_SAW_REVERSED: {
      previousType = LFO_SAW_REVERSED;
      UInt samplesToProcess = result.getLength();
      Flt * out = result.getPtr();
      Flt * in = LfoSawTable.getPtr();
      while (samplesToProcess) {
        *out++ = in[(UInt)cursor];
        cursor -= frequency;
        if (cursor < 0) cursor += SAMPLERATE;
        samplesToProcess--;
      }
      break;
    }

    case LFO_TRIANGLE: {
      previousType = LFO_TRIANGLE;
      UInt samplesToProcess = result.getLength();
      Flt * out = result.getPtr();
      Flt * in = LfoTriangleTable.getPtr();
      while (samplesToProcess) {
        *out++ = in[(UInt)cursor];
        cursor += frequency;
        if (cursor >= SAMPLERATE) cursor -= SAMPLERATE;
        samplesToProcess--;
      }
      break;
    }

    case LFO_SINE: {
      previousType = LFO_SINE;
      UInt samplesToProcess = result.getLength();
      Flt * out = result.getPtr();
      Flt * in = LfoTriangleTable.getPtr();
      while (samplesToProcess) {
        *out++ = in[(UInt)cursor];
        cursor += frequency;
        if (cursor >= SAMPLERATE) cursor -= SAMPLERATE;
        samplesToProcess--;
      }
      break;
    }
  }

  previousLineValue = result.getBack();
  return result;
}

