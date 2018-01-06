/*
  ==============================================================================

    sineWave.cpp
    Created: 31 Jan 2014 2:56:47pm
    Author:  yvan

  ==============================================================================
*/

#include "sineWave.hpp"
#include "../../utils/misc.hpp"
#include "../drawableBuffer.hpp"

namespace YSE {
  namespace DSP {
    class sineWaveImpl {
    public:
      sine sineGen;
      drawableBuffer volumeCurve;

      buffer frequencyCurve;
      aFlt frequency;
      Flt currentFrequency;

      sineWaveImpl() : frequency(0), currentFrequency(0) {
        // don't make the mistake of putting this in the initialisation list:
        // it would call the constructors and set the buffer sizes to zero, instead of the values
        volumeCurve = 0.f;
        frequencyCurve = 0.f;
      }
    };

    void YSE::DSP::sineWave::process(SOUND_STATUS & intent, Int & latency) {
      Flt lastVolume = volumeCurve.getBack();
      Clamp(lastVolume, 0.f, 1.f);

      if (latency >= STANDARD_BUFFERSIZE) {
        // no new attack, but a previous curve might not be done yet
        if (intent == SS_STOPPED || intent == SS_PAUSED) {
          // complete fade out if needed
          if (lastVolume > 0) {
            volumeCurve.drawLine(0, (UInt)(lastVolume * 200), lastVolume, 0);
            volumeCurve.drawLine((UInt)((lastVolume)* 200), STANDARD_BUFFERSIZE, 0);
          }
          else {
            volumeCurve = 0.f;
          }
        }
        else if (intent == SS_PLAYING) {
          // complete fade in if needed
          if (lastVolume < 1) {
            volumeCurve.drawLine(0, (UInt)((1 - lastVolume) * 200), lastVolume, 1);
            volumeCurve.drawLine((UInt)((1 - lastVolume) * 200), STANDARD_BUFFERSIZE, 1);
          }
          else {
            volumeCurve = 1.f;
          }
        }
        else {
          // in all other cases continue the previous volume status
          volumeCurve = lastVolume;
        }

        // frequency can be changed within a note, but if intent != playing, we assume
        // it will change on the next attack
        if (intent != SS_PLAYING) {
          // just keep the last freqency
          frequencyCurve = currentFrequency;
        }
        else {
          currentFrequency = parmFrequency;
          frequencyCurve = currentFrequency;
        }

      }
      else if (latency == 0) {
        // no latency at all, just do everything at the beginning of the sample
        if (intent == SS_STOPPED || intent == SS_PAUSED) {
          // complete fade out if needed
          if (lastVolume > 0) {
            volumeCurve.drawLine(0, (UInt)(lastVolume * 200), lastVolume, 0);
            volumeCurve.drawLine((UInt)((lastVolume)* 200), STANDARD_BUFFERSIZE, 0);
          }
          else {
            volumeCurve = 0.f;
          }
          frequencyCurve = currentFrequency;
        }
        else if (intent == SS_PLAYING) {
          // complete fade in if needed
          if (lastVolume < 1) {
            volumeCurve.drawLine(0, (UInt)((1 - lastVolume) * 200), lastVolume, 1);
            volumeCurve.drawLine((UInt)((1 - lastVolume) * 200), STANDARD_BUFFERSIZE, 1);
          }
          else {
            volumeCurve = 1.f;
          }
          frequencyCurve = currentFrequency;
        }
        else if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
          volumeCurve.drawLine(0, 200, lastVolume, 1);
          volumeCurve.drawLine(200, STANDARD_BUFFERSIZE, 1);
          currentFrequency = parmFrequency;
          frequencyCurve = currentFrequency;
          intent = SS_PLAYING;
        }
        else if (intent == SS_WANTSTOPAUSE || intent == SS_WANTSTOSTOP) {
          volumeCurve.drawLine(0, 200, lastVolume, 0);
          volumeCurve.drawLine(200, STANDARD_BUFFERSIZE, 0);
          frequencyCurve = currentFrequency;
          intent = SS_STOPPED;
        }


      }
      else {
        // latency > 0, but smaller as buffersize
        // this means the change has to come somewhere in the middle of the sample

        // no new attack, but a previous curve might not be done yet
        if (intent == SS_STOPPED || intent == SS_PAUSED) {
          // complete fade out if needed
          if (lastVolume > 0) {
            volumeCurve.drawLine(0, (UInt)(lastVolume * 200), lastVolume, 0);
            volumeCurve.drawLine((UInt)((lastVolume)* 200), STANDARD_BUFFERSIZE, 0);
          }
          else {
            volumeCurve = 0.f;
          }
          frequencyCurve = currentFrequency;

        }
        else if (intent == SS_PLAYING) {
          // complete fade in if needed
          if (lastVolume < 1) {
            volumeCurve.drawLine(0, (UInt)((1 - lastVolume) * 200), lastVolume, 1);
            volumeCurve.drawLine((UInt)((1 - lastVolume) * 200), STANDARD_BUFFERSIZE, 1);
          }
          else {
            volumeCurve = 1.f;
          }
          frequencyCurve = currentFrequency;

        }
        else if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
          Int slopeLength = (latency + 200) >= STANDARD_BUFFERSIZE ? STANDARD_BUFFERSIZE - latency : 200;
          volumeCurve.drawLine(0, latency, lastVolume);
          volumeCurve.drawLine(latency, latency + slopeLength, lastVolume, slopeLength * 0.005f);
          if ((latency + slopeLength) < STANDARD_BUFFERSIZE) {
            volumeCurve.drawLine(latency + slopeLength, STANDARD_BUFFERSIZE, 1);
          }
          // change frequency after latency
          frequencyCurve.drawLine(0, latency, currentFrequency);
          currentFrequency = parmFrequency;
          frequencyCurve.drawLine(latency, STANDARD_BUFFERSIZE, currentFrequency);

          intent = SS_PLAYING;

        }
        else if (intent == SS_WANTSTOSTOP || intent == SS_WANTSTOPAUSE) {
          Int slopeLength = latency + 200 > STANDARD_BUFFERSIZE ? STANDARD_BUFFERSIZE - latency : 200;
          volumeCurve.drawLine(0, latency, lastVolume);
          volumeCurve.drawLine(latency, latency + slopeLength, lastVolume, 1 - slopeLength * 0.005f);
          if (latency + slopeLength < STANDARD_BUFFERSIZE) {
            volumeCurve.drawLine(latency + slopeLength, STANDARD_BUFFERSIZE, 0);
          }
          // we don't care about frequency here because it's inaudible anyway
          frequencyCurve = currentFrequency;
          intent = SS_STOPPED;
        }
      }


      YSE::DSP::buffer & sin = sineGen(frequencyCurve);
      for (UInt i = 0; i < samples.size(); i++) {
        samples[i] = sin;
        samples[i] *= volumeCurve;
      }
      latency -= STANDARD_BUFFERSIZE;
      if (latency < 0) latency = 0;
    }

    YSE::DSP::sineWave::sineWave() : parmFrequency(0), currentFrequency(0) {
      // don't make the mistake of putting this in the initialisation list:
      // it would call the constructors and set the buffer sizes to zero, instead of the values
      volumeCurve = 0.f;
      frequencyCurve = 0.f;
    }

    void YSE::DSP::sineWave::frequency(Flt value) {
      parmFrequency = value;
    }

    Flt YSE::DSP::sineWave::frequency() {
      return parmFrequency;
    }



  }
}
