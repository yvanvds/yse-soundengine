DSP Objects
===========

YSE offers 2 main uses for DSP: sound generation and filtering. (DSP can also be used for virtual synths, which is discussed in another chapter.) Please take great care when writing your own DSP objects. An inefficient DSP process function can slow down your software considerably. _Avoid memory allocation during DSP processing at all cost!_

DSP Filter Objects
==================

Filter objects should be subclasses of `YSE::DSP::dspObject`. This is a virtual base class for which you should implement at least the process function. This function will be called during the audio rendering, which is done in a separate thread.

The process function is where all the sound manipulation happens. You get a reference to a multichannel audio buffer, which can be modified as to apply the filter. The ringModulator, provided in the source code, can be used as an example.

~~~~{.cpp}
namespace YSE {
  namespace DSP {
    class API ringModulator : dspObject {
    public:
      ringModulator(): parmFrequency(440.f), parmLevel(0.5f) {}

      void frequency(Flt value){
        parmFrequency = value;
      }
      
      Flt frequency() {
        return parmFrequency();
      }
      
      
      void level(Flt value) {
        parmLevel = value;
      }
      
      Flt level() {
        return parmLevel;
      }

      // dsp function
      virtual void process(MULTICHANNELBUFFER & buffer) {
        // resizes only if needed
        extra.resize(buffer[0].getLength());

        // generate sine wave at wanted frequency
        AUDIOBUFFER & sin = sineGen(parmFrequency, buffer[0].getLength());

        Flt level = parmLevel;
        for (UInt i = 0; i < buffer.size(); i++) {
          // make a copy of the original, this will add the dry sound to the output
          extra = buffer[i];
          // adjust volume of dry sound
          extra *= static_cast<Flt>(1 - level);
          // combine input with sine to get a ring modulator effect
          buffer[i] *= sin;
          // adjust volume of wet sound
          buffer[i] *= level;

          // combine wet and dry sound 
          buffer[i] += extra;
        }
      }

    private:
      // these are atomic because they are used in
      // the process function but can be changed from the
      // interface
      aFlt parmFrequency;
      aFlt parmLevel;
      
      // sine and sample are provided DSP primitives
      sine sineGen;
      sample extra;
    };

  }
}
~~~~

If variables are used both in the process function and in functions that will be called from the main thread, it is up to you to make them thread safe. The easiest way to do this is by using `std::atomic`, or by use of the already provided atomic versions of basic variables: `aInt`, `aBool`, `aFlt` and `aVec`. Locking mechanisms can also be used but should be avoided whenever possible.

Filter objects can be added to sounds and channels. (They can also be chained).

~~~~{.cpp}
YSE::DSP::ringModulator ring;
YSE::sound sound;

sound.create("bla.wav").setDSP(&ring);
~~~~

DSP Source Objects
==================

Source objects are quite related to filter objects, but are used to generate sound. For this reason the process function is a bit different. The audio buffer will not be passed as an argument, but exists within the class. It is up to you to fill it.

The process function does have an YSE::SOUND_STATUS as an argument. This indicates if the engine wants to start, pause or stop the sound.

Another function you will have to implement is the frequency function. This function will be called by the engine to adjust the frequency of your sound source. It is mainly used to create a Doppler effect when needed. You should not used it for basic frequency changes.

DSP Primitives
==============
YSE provides a lot of primitive dsp functions and classes. These can be used to build more complex filters and generators without having to start from scratch. Most primitives do their DSP thing in the operator() function. This means you can use them quite easily:

~~~~{.cpp}
// object definition somewhere in your class	
YSE::DSP::sine mySine;

// assign sinewave in the process function
buffer = mySine(440);
~~~~

Sample
------

A sample contains a block of audio, and by default uses the standard buffersize which is used for audio rendering. It is used to pass an audio block to most other dsp classes. 

Basic operators like +,-,/ and * can also be used with samples.

Oscillators
-----------

Standard oscillators are Sine, Cosine, Saw and Noise. They can be used to render an audiobuffer according to a given frequency. Sine and Saw can also take another audiobuffer as input. In this case, the buffer is supposed to contain the frequency for every sample.

Another class in this group is vcf, which is actually a filter but put together with oscillators because of the way it works internally.

Filters
-------

The supplied filter objects are lowpass, highpass, bandpass and a biQuad filter. The frequency for these filters can be set with setFrequency. The operator() argument is an mono audiobuffer.

The bandpass filter also has a Q value. The biQuad filter is a bit harder to deal with, because the filter itself is more complicated. If it doesn't seem to make much sense, it is probably time to read a bit about biQuad filters first. A nice website for visualizing biquad parameters, is  http://www.earlevel.com/main/2010/12/20/biquad-calculator/

The last class in the filter category is a sample & hold class.

Delay
-----

Ramp
----

Ramps can be used to change a value over time. The resulting audio buffer will not contain a real audio signal, but can be applied to other audio buffers.

~~~~{.cpp}
// in class definition:
YSE::DSP::sine sine;
YSE::DSP::ramp ramp;

// some trigger to set up the ramp
if(trigger) {
  ramp.set(1, 100); // go from 0 to one over 100 milliseconds	
}

ramp.update();
buffer = sine(440);
buffer *= ramp();
~~~~

A simplified version of ramp is lint (linear interpolator). The internal value also moves towards its target, but is applied on a float instead of an audio buffer.

These classes aside, a few functions also can be applied to audiobuffers, as long as the required manipulation takes at most the length of one buffer: 

~~~~{.cpp}
void FastFadeIn(sample& s, UInt length);
void FastFadeOut(sample& s, UInt length);
void ChangeGain(sample& s, Flt currentGain, Flt newGain, UInt length);
~~~~

Math
----


