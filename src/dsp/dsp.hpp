#pragma once
#include "sample.hpp"
#include <vector>
#include "../headers/enums.hpp"

#if WINDOWS
#pragma warning(disable:4251)
#endif

#define BUFFER std::vector<DSP::sample> &

namespace YSE {
	namespace DSP {

    // simple base class for a chainable dsp object

		class API dsp {
    public:
			virtual void process(BUFFER buffer) = 0;

      // link the output of this dsp to another dsp.
      // If the dsp is already linked, the new dsp will
      // be put between this object and the current next
      // object.
      void link(dsp& next);
      dsp * link();

      dsp();
     ~dsp();

     dsp& bypass(Bool value ) {_bypass = value; return *this; }
     Bool bypass(           ) { return _bypass; }

     dsp ** calledfrom; // consider this private for now
    private:
      dsp * next;
      dsp * previous;
      Bool _bypass;

		};

    // simple base class for a dsp object with sound generation
    // DSPSource can be by sounds for sound generation. This is
    // why some virtual functions have to be implemented.

		class API dspSource {
    public:
			std::vector<sample> buffer;
			dspSource(Int buffers = 1);

      // intent is what we should do (playing, start playing, start stopping etc...
      // latency is after how many samples this should happen
			virtual void process(SOUND_STATUS & intent, Int & latency) = 0;
      virtual void frequency(Flt value) = 0;
		};

	}
}
