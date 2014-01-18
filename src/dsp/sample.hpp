#pragma once
#include "../utils/memory.hpp"

/*
    - Sample contains a sound buffer. If you only need a reference to
      an existing buffer (for instance within another dsp class) you
      should use SamplePtr instead because it won't create a copy of
      the buffer.
    - Don't create samples as local variables because they have to
      allocate dynamic memory.
    - Don't use samplePtr and sample outside DSP if you can avoid it.
    - Only the length functions are threadsafe.
*/

#define BUFFERSIZE 512
#define SAMPLE const YSE::DSP::sample&

namespace YSE {
	extern UInt sampleRate;

	namespace DSP {
    class sampleImpl;
		class API sample {
		public:
      sample(UInt size = BUFFERSIZE);
			sample(SAMPLE cp);
		 ~sample();

			UInt  getLength   () const;
			UInt	getLengthMS () const;
			Flt		getLengthSec() const;

      Flt * getBuffer   () const; // try to avoid this function. It will give you direct access
                                  // to the sample buffer, but memory can be overwritten when
                                  // you don't use the correct length of the buffer (getlength)

			SAMPLE operator+=(Flt f);
			SAMPLE operator-=(Flt f);
			SAMPLE operator*=(Flt f);
			SAMPLE operator/=(Flt f);

			SAMPLE operator+=(SAMPLE s);
			SAMPLE operator-=(SAMPLE s);
			SAMPLE operator*=(SAMPLE s);
			SAMPLE operator/=(SAMPLE s);

			SAMPLE operator=(SAMPLE s);
			SAMPLE operator=(Flt f);
			SAMPLE copyFrom (SAMPLE s, Int SourcePos, Int DestPos, Int length);

      // functions to 'draw' directly into the buffer
      sample& drawLine(UInt start, UInt stop, Flt startValue, Flt stopValue); // slope
      sample& drawLine(UInt start, UInt stop, Flt value); // horizontal line
      Flt getBack(); // returns the value at the end of the buffer

      // TODO: what was i thinking?? Let's see if anyone needs this
      //const sample& getPart  (const sample& s, UInt start, UInt length);

      // Each sample holds an internal cursor. You can use this to remember a
      // buffer position.
      Flt * cursor;

      // resize a sample buffer, copy current contents if needed
      // copy will fill remaining values with zeroes
      // with copy = false, the buffer values are not initialized
      SAMPLE resize(UInt length, Bool copy = false);
    private:
      sampleImpl * impl;
		};

		/*class API sample : public samplePtr {
    public:
			sample(Int size = BUFFERSIZE);
			sample(const sample& cp);
		 ~sample();

			 // grow sample buffer to this length

			sample& operator=(const samplePtr& s); // makes a real copy of the provided sample
			sample& operator=(const sample& s);
			sample& operator=(Flt f) { __super::operator=(f); return (*this);}
		};*/


	}
}

