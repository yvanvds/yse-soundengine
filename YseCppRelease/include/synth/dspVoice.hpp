/*
  ==============================================================================

    dspVoice.h
    Created: 10 Jul 2014 6:08:28pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DSPVOICE_H_INCLUDED
#define DSPVOICE_H_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "../dsp/dspObject.hpp"
#include "../dsp/math.hpp"
//
//namespace YSE {
//  namespace SYNTH {
//
//    class API dspVoice : public DSP::dspSourceObject {
//    public:
//      dspVoice();
//      virtual ~dspVoice() {}
//
//      // intent is what we should do (playing, start playing, start stopping etc...
//      virtual void process(SOUND_STATUS & intent) = 0;
//
//      Flt getFrequency() { return _frequency.load(); }
//      Flt getVelocity () { return _velocity .load(); }
//
//      //this will be used internally to pass the midi note number
//      void frequency(Flt value) {
//        _frequency.store(DSP::MidiToFreq(value));
//      }
//
//      void velocity(Flt value) {
//        _velocity.store(value);
//      }
//
//      // this function is used internally. It should return a base class pointer
//      // to a dynamically created object of your derived class. For instance, if
//      // your derived class is called 'derived', write the function like this:
//      //
//      // virtual dspVoice * clone() {
//      //   return new derived();
//      // }
//      virtual dspVoice * clone() = 0;
//
//    private:
//      aFlt _frequency;
//      aFlt _velocity ;
//    };
//
//  }
//}



#endif  // DSPVOICE_H_INCLUDED
