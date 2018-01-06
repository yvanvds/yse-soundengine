/*
  ==============================================================================

    reverbDSP.h
    Created: 1 Feb 2014 6:58:46pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBDSP_H_INCLUDED
#define REVERBDSP_H_INCLUDED

#include <vector>
#include "../dsp/delay.hpp"
#include "../dsp/modules/hilbert.hpp"
#include "../dsp/oscillators.hpp"
#include "../dsp/ramp.hpp"
#include "../dsp/dspObject.hpp"
#include "../reverb/reverb.hpp"

#define COMBS 8
#define	APASS	4

namespace YSE {
  namespace INTERNAL {

    class reverbChannel {
    public:
      void clear();
      void update();

      DSP::buffer   out;
      DSP::delay    delayline;
      DSP::buffer   early[4];
      DSP::ramp     earlyPtr[4];
      DSP::ramp     earlyVolume[4];
      DSP::hilbert  hil;
      DSP::buffer   hil1;
      DSP::buffer   hil2;

      Flt * cPtr;
      Int   earlyOffset;
      Flt   filterStore[COMBS];
      std::vector< std::vector<Flt> > bufComb;
      Int   combIndex[COMBS];
      Int   combTuning[COMBS];
      std::vector< std::vector<Flt> > bufAll;
      Int   allIndex[APASS];
      Int   allTuning[APASS];

      reverbChannel();
      reverbChannel(const reverbChannel & source);
    };

    class reverbDSP : DSP::dspObject {
    public:
      Flt _gain;
      Flt _roomsize1;
      Flt _damp1;
      Flt _wet, _wet1, _wet2;
      Bool _freeze;
      Bool _bypass;

      // faders for smooth value adjustment 
      DSP::lint _roomsizeFader;
      DSP::lint _dampFader;
      DSP::lint _wetFader;
      DSP::lint _dryFader;
      DSP::lint _widthFader;

      // modulation
      DSP::sine   sinGen;
      DSP::saw    sawGen;
      DSP::cosine cos1, cos2;
      DSP::buffer modPtr;
      DSP::buffer cosPtr1;
      DSP::buffer cosPtr2;
      DSP::lint  _modFrequency;
      DSP::lint  _modWidth;

      void modulate(Flt frequency, Flt width);


      std::vector<reverbChannel> channel;
      void channels(Int value);

      // set - get
      void combDamp(Flt value);

      reverbDSP& combFeedback(Flt value);  Flt combFeedback();
      reverbDSP& allpassFeedback(Flt value);  Flt allpassFeedback();

      reverbDSP& bypass(Bool value);		Bool bypass();
      reverbDSP& roomSize(Flt  value);	  Flt  roomSize();
      reverbDSP& damp(Flt  value);		Flt  damp();
      reverbDSP& wet(Flt  value);		Flt  wet();
      reverbDSP& dry(Flt  value);		Flt  dry();
      reverbDSP& width(Flt  value);		Flt  width();
      reverbDSP& freeze(Bool value);		Bool freeze();

      void set(reverb & impl);

      virtual void create() {}
      virtual void process(MULTICHANNELBUFFER & buffer);
      void update();
      reverbDSP();
      ~reverbDSP();

    };

  }
}



#endif  // REVERBDSP_H_INCLUDED
