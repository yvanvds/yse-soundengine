#include "stdafx.h"
#include "sineWave.hpp"
#include "../implementations.h"
#include "dsp/ramp.hpp"
#include "utils/misc.hpp"

namespace YSE {
  namespace DSP {
    class sineWaveImpl {
    public:
			sine sineGen;
      sample volumeCurve;

      sample frequencyCurve;
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
      Flt lastVolume = impl->volumeCurve.getBack();
      Clamp(lastVolume,  0, 1);

      if (latency >= BUFFERSIZE) {
        // no new attack, but a previous curve might not be done yet
        if (intent == SS_STOPPED || intent == SS_PAUSED) {
          // complete fade out if needed
          if (lastVolume > 0) {
            impl->volumeCurve.drawLine(0, (UInt)(lastVolume * 200), lastVolume, 0);
            impl->volumeCurve.drawLine((UInt)((lastVolume) * 200), BUFFERSIZE, 0);
          } else {
            impl->volumeCurve = 0.f;
          }
        } else if (intent == SS_PLAYING) {
          // complete fade in if needed
          if (lastVolume < 1) {
            impl->volumeCurve.drawLine(0, (UInt)((1 - lastVolume) * 200), lastVolume, 1);
            impl->volumeCurve.drawLine((UInt)((1 - lastVolume) * 200), BUFFERSIZE, 1);
          } else {
            impl->volumeCurve = 1.f;
          }
        } else {
          // in all other cases continue the previous volume status
          impl->volumeCurve = lastVolume;
        }

        // frequency can be changed within a note, but if intent != playing, we assume
        // it will change on the next attack
        if (intent != SS_PLAYING) {
          // just keep the last freqency
          impl->frequencyCurve = impl->currentFrequency;
        } else {
          impl->currentFrequency = impl->frequency;
          impl->frequencyCurve = impl->currentFrequency; 
        }
        
      } else if (latency == 0) {
        // no latency at all, just do everything at the beginning of the sample
        if (intent == SS_STOPPED || intent == SS_PAUSED) {
          // complete fade out if needed
          if (lastVolume > 0) {
            impl->volumeCurve.drawLine(0, (UInt)(lastVolume * 200), lastVolume, 0);
            impl->volumeCurve.drawLine((UInt)((lastVolume) * 200), BUFFERSIZE, 0);
          } else {
            impl->volumeCurve = 0.f;
          }
          impl->frequencyCurve = impl->currentFrequency;
        } else if (intent == SS_PLAYING) {
          // complete fade in if needed
          if (lastVolume < 1) {
            impl->volumeCurve.drawLine(0, (UInt)((1 - lastVolume) * 200), lastVolume, 1);
            impl->volumeCurve.drawLine((UInt)((1 - lastVolume) * 200), BUFFERSIZE, 1);
          } else {
            impl->volumeCurve = 1.f;
          }
          impl->frequencyCurve = impl->currentFrequency;
        } else if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
          impl->volumeCurve.drawLine(0, 200, lastVolume, 1);
          impl->volumeCurve.drawLine(200, BUFFERSIZE, 1);
          impl->currentFrequency = impl->frequency;
          impl->frequencyCurve = impl->currentFrequency;
          intent = SS_PLAYING;
        } else if (intent == SS_WANTSTOPAUSE || intent == SS_WANTSTOSTOP) {
          impl->volumeCurve.drawLine(0, 200, lastVolume, 0);
          impl->volumeCurve.drawLine(200, BUFFERSIZE, 0);
          impl->frequencyCurve = impl->currentFrequency;
          intent = SS_STOPPED;
        } 
        
        
      } else {
        // latency > 0, but smaller as buffersize
        // this means the change has to come somewhere in the middle of the sample

        // no new attack, but a previous curve might not be done yet
        if (intent == SS_STOPPED || intent == SS_PAUSED) {
          // complete fade out if needed
          if (lastVolume > 0) {
            impl->volumeCurve.drawLine(0, (UInt)(lastVolume * 200), lastVolume, 0);
            impl->volumeCurve.drawLine((UInt)((lastVolume) * 200), BUFFERSIZE, 0);
          } else {
            impl->volumeCurve = 0.f;
          }
          impl->frequencyCurve = impl->currentFrequency;
        
        } else if (intent == SS_PLAYING) {
          // complete fade in if needed
          if (lastVolume < 1) {
            impl->volumeCurve.drawLine(0, (UInt)((1 - lastVolume) * 200), lastVolume, 1);
            impl->volumeCurve.drawLine((UInt)((1 - lastVolume) * 200), BUFFERSIZE, 1);
          } else {
            impl->volumeCurve = 1.f;
          }
          impl->frequencyCurve = impl->currentFrequency;
        
        } else if (intent == SS_WANTSTOPLAY || intent == SS_WANTSTORESTART) {
          Int slopeLength = (latency + 200) >= BUFFERSIZE ? BUFFERSIZE - latency : 200;
          impl->volumeCurve.drawLine(0, latency, lastVolume);
          impl->volumeCurve.drawLine(latency, latency + slopeLength, lastVolume, slopeLength * 0.005f);
          if ((latency + slopeLength) < BUFFERSIZE) {
            impl->volumeCurve.drawLine(latency + slopeLength, BUFFERSIZE, 1);
          }
          // change frequency after latency
          impl->frequencyCurve.drawLine(0, latency, impl->currentFrequency);
          impl->currentFrequency = impl->frequency;
          impl->frequencyCurve.drawLine(latency, BUFFERSIZE, impl->currentFrequency);
          
          intent = SS_PLAYING;

        } else if (intent == SS_WANTSTOSTOP || intent == SS_WANTSTOPAUSE) {
          Int slopeLength = latency + 200 > BUFFERSIZE ? BUFFERSIZE - latency : 200;
          impl->volumeCurve.drawLine(0, latency, lastVolume);
          impl->volumeCurve.drawLine(latency, latency + slopeLength, lastVolume, 1 - slopeLength * 0.005f);
          if (latency + slopeLength < BUFFERSIZE) {
            impl->volumeCurve.drawLine(latency + slopeLength, BUFFERSIZE, 0);
          }
          // we don't care about frequency here because it's inaudible anyway
          impl->frequencyCurve = impl->currentFrequency;
          intent = SS_STOPPED;
        }
      }
        
       
      SAMPLE sin = impl->sineGen(impl->frequencyCurve);
	    for (UInt i = 0; i < buffer.size(); i++) {
		    buffer[i] = sin;
        buffer[i] *= impl->volumeCurve;
	    }
      latency -= BUFFERSIZE;
      if (latency < 0) latency = 0;
    }

    YSE::DSP::sineWave::sineWave() : impl(new sineWaveImpl) {}

    YSE::DSP::sineWave::~sineWave() {
      delete impl;
    }

    void YSE::DSP::sineWave::frequency(Flt value) {
      impl->frequency = value;
    }

    Flt YSE::DSP::sineWave::frequency() {
      return impl->frequency;
    }

    

  }
}
