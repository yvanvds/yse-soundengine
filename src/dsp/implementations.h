#pragma once
#include <atomic>
#include "headers/defines.hpp"
#include "dsp/dsp.hpp"
#include "dsp/oscillators.hpp"

namespace YSE {
  namespace DSP {

    class sampleImpl {
    public:
      aUInt length;
      Flt * buffer;
      sampleImpl(UInt length) : length(length), buffer(new Flt[length]) {}
    };

    class rampImpl {
    public:  
      aFlt target   ;
			aFlt time     ;
			aFlt current  ;
			aInt ticksLeft;
			aBool reTarget;

			Flt _1overN       ;
			Flt _dspTickToMSEC;
			Flt inc, bigInc   ;
      rampImpl() : ticksLeft(0), reTarget(false), current(0), target(0), time(0) {}
    };

    class lintImpl {
    public:
      aFlt target, current, step;
      aBool up, calculate;
      Flt stepSecond;
      lintImpl() : target(0), current(0), step(0), up(false), calculate(false) {
        stepSecond = sampleRate / (Flt)BUFFERSIZE; 
      }
    };

    class clipImpl {
    public:
      aFlt low;
      aFlt high;
      sample buffer;
      clipImpl() : low(-1.0f), high(-1.0f) {}
    };


    class filterImpl {
    public:
      aFlt freq;
			aFlt gain;
      Flt q;
			Flt last;
			Flt previous;
			aFlt coef1, coef2;
      aFlt ff1, ff2, ff3, fb1, fb2;
			sample buffer;
      filterImpl() :  freq(0), gain(0), q(0), 
                      last(0), previous(0), 
                      coef1(0), coef2(0), 
                      ff1(0), ff2(0), ff3(0), fb1(0), fb2(0) {}
    };

    class sHoldImpl {
    public:
      aFlt lastIn, lastOut;
      sample buffer;
    };


    #define XTRASAMPS 4
    #define DEFDELVS 64
    #define SAMPBLK 4
    class delayImpl {
    public:
      UInt bufferlength;
			Flt * buffer;
			Int phase;

			UInt currentLength; // the sample length for this loop
			aUInt size;
      delayImpl(Int size) : bufferlength(0), buffer(new Flt[size + XTRASAMPS]) {} 
     ~delayImpl() { delete[] buffer; }
    };

    class ringModImpl {
    public:
      aFlt frequency;
			aFlt level;
      sine sineGen;
			sample extra;
      ringModImpl() : frequency(440.f), level(0.5f) {}
    };


  }
}

