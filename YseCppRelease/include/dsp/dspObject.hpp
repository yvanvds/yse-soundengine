/*
  ==============================================================================

    source.h
    Created: 31 Jan 2014 2:53:05pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SOURCE_H_INCLUDED
#define SOURCE_H_INCLUDED


#include <vector>
#include <memory>
#include "buffer.hpp"
#include "../headers/enums.hpp"
#include "lfo.hpp"
#include "math.hpp"

namespace YSE {
  namespace SOUND {
    class implementationObject;
  }

  namespace DSP {
    // simple base class for a chainable dsp object
    class API dspObject {
    public:
      dspObject();
      virtual ~dspObject();

      
      virtual void create() = 0;
      virtual void process(MULTICHANNELBUFFER & buffer) = 0;

      // link the output of this dsp to another dsp.
      // If the dsp is already linked, the new dsp will
      // be put between this object and the current next
      // object.
      void link(dspObject& next);
      dspObject * link();

      dspObject& bypass(Bool value) { _bypass = value; return *this; }
      Bool       bypass() { return _bypass; }

      // impact of this filter. Must be between 0 and 1
      dspObject& impact(Flt value) { _impact = value; return *this; }
      Flt impact() { return _impact; }

      dspObject & lfoType(LFO_TYPE type) { _lfoType = type; return *this; }
      LFO_TYPE lfoType() { return _lfoType; }

      dspObject & lfoFrequency(Flt value) { _lfoFrequency = value; return *this; }
      Flt lfoFrequency() { return _lfoFrequency; }

      dspObject ** calledfrom; // consider this private for now

    protected:
      // call this at start of process()
      void createIfNeeded();
      // call this in process to get current lfo buffer
      buffer & getLFO();
      // call this at end of process()
      void calculateImpact(buffer & in, buffer & filtered);
      

    private:
      dspObject * next;
      dspObject * previous;
      Bool _bypass;
      Bool _needsCreate;
      aFlt _impact;

      std::shared_ptr<lfo> lfoOsc;
      std::shared_ptr<inverter> invertedImpact;
      LFO_TYPE _lfoType;
      aFlt _lfoFrequency;
    };

    // simple base class for a dsp object with sound generation
    // DSPSource can be by sounds for sound generation. This is
    // why some virtual functions have to be implemented.
    class API dspSourceObject {
    public:
      std::vector<buffer> samples;
      dspSourceObject(Int buffers = 1);

      // intent is what we should do (playing, start playing, start stopping etc...
      virtual void process(SOUND_STATUS & intent) = 0;
      virtual void frequency(Flt value) = 0;
    };

  }
}




#endif  // SOURCE_H_INCLUDED
