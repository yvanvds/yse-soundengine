#pragma once
#include "dsp/dsp.hpp"
#include "utils/misc.hpp"
#include "dsp/delay.hpp"
#include "dsp/modules/hilbert.hpp"
#include "dsp/oscillators.hpp"
#include "dsp/sample.hpp"
#include "internal/reverbimpl.h"
#include "dsp/ramp.hpp"

#define COMBS 8
#define	APASS	4

namespace YSE {
	extern UInt sampleRate;

	struct reverbChannel {
		DSP::sample out;
		DSP::delay delayline;
	  DSP::sample early[4];
		DSP::ramp earlyPtr[4];
		DSP::ramp earlyVolume[4];
		Int earlyOffset;
		DSP::hilbert hil;
	  DSP::sample hil1;
		DSP::sample hil2;
		Flt * cPtr;
		Flt filterStore[COMBS];
		Flt * bufComb[COMBS];
		Int combIndex[COMBS];
		Int combTuning[COMBS];
		Flt * bufAll[APASS];
		Int allIndex[APASS];
		Int allTuning[APASS];
		reverbChannel();
		~reverbChannel();
		void clear();
    void update();
	};

	class reverbBackend : DSP::dsp {
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
	  DSP::sine sinGen;
	  DSP::saw sawGen;
	  DSP::cosine cos1, cos2;
    DSP::sample modPtr;
    DSP::sample cosPtr1;
	  DSP::sample cosPtr2;
	  DSP::lint _modFrequency;
	  DSP::lint _modWidth;
	  void modulate(Flt frequency, Flt width);


	  reverbChannel * channel;
	  Int numChannels;
	  void channels(Int value);

	  // set - get
	  void combDamp(Flt value);
	  reverbBackend& combFeedback(Flt value);
	  Flt combFeedback();
	  reverbBackend& allpassFeedback(Flt value);
	  Flt allpassFeedback();
	  reverbBackend& bypass(Bool value);		Bool bypass();
	  reverbBackend& roomSize(Flt value);	  Flt roomSize();
	  reverbBackend& damp(Flt value);			  Flt damp();
	  reverbBackend& wet(Flt value);				Flt wet();
	  reverbBackend& dry(Flt value);				Flt dry();
	  reverbBackend& width(Flt value);			Flt width();
	  reverbBackend& freeze(Bool value);		Bool freeze();
	  void set(YSE::reverbimpl & impl);

	  virtual void process(BUFFER buffer);
	  void update();
	  reverbBackend();
   ~reverbBackend();

			
	};

	extern reverbBackend ReverbBackend;
}