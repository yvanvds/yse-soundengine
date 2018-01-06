#pragma once
#include "basePage.h"
#include "yse.hpp"

// inherit your dsp class from dspSource
class shepard : public YSE::DSP::dspSourceObject {
public:
  // process function is pure virtual in dspSource
  // you HAVE to implement it
  virtual void process(YSE::SOUND_STATUS & intent);
  // constructor can be implement if you need it
  // (you probably will)
  shepard();
  void frequency(Flt value);
  Flt frequency();

private:
  // in this case we add:
  // a sample buffer to hold the sum of all generators
  YSE::DSP::buffer out;
  // sinewave generators
  YSE::DSP::sine generators[11];
  // frequencies for all generators
  Flt freq[11];
  // the maximum frequency
  Flt top;
  // the current volume for output (this is adjusted according to SOUND_STATUS
  Flt volume;

  YSE::DSP::lowPass lp;

  Flt lpFreq;
  YSE::DSP::buffer s1, s2;
};


class DemoDspSource :
  public basePage
{
public:
  DemoDspSource();
  ~DemoDspSource();

  virtual void ExplainDemo();

  void FilterUp();
  void FilterDown();

private:
  shepard s;
  YSE::sound sound;
  
};

