/*
  ==============================================================================

    classes.hpp
    Created: 6 Feb 2014 6:47:00pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CLASSES_HPP_INCLUDED
#define CLASSES_HPP_INCLUDED

namespace YSE {
  // public classes
  class channel;
  class listener;
  class log;
  class sound;
  class system;

  // tools
  class Vec;
  class aVec;
 
  namespace DSP {
    // public dsp classes
    class sample;
    class dspObject;
    class dspSourceObject;

    // filters
    class highPass;
    class lowPass;
    class bandPass;
    class biQuad;
    class sampleHold;
    class ramp;
    class lint;
    class delay;

    // oscillators
    class saw;
    class cosine;
    class sine;
    class noise;
    class vcf;
    
    // math functions
    class clip;
    class rSqrt;
    class sqrt;
    class wrap;
    class midiToFreq;
    class freqToMidi;
    class dbToRms;
    class rmsToDb;
    class dbToPow;
    class powToDb;
    class pow;
    class exp;
    class log;
    class abs;

    // modules
    class hilbert;
    class ringModulator;
    class sineWave;
  }

  namespace INTERNAL {
    // internal classes
    class global;
    class channelManager;
    class deviceManager;
    class reverbManager;
    class soundManager;
    class reverbChannel;
    class channelImplementation;
    class listenerImplementation;
    class soundImplementation;
    class logImplementation;
    class output;
    class settings;
    class reverbDSP;
    class soundFile;
    class time;
    class underWaterEffect;
  }
}



#endif  // CLASSES_HPP_INCLUDED
