#include "AudioTest.h"

YSE::INTERNAL::AudioTest & YSE::INTERNAL::Test() {
  static AudioTest s;
  return s;
}

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

  virtual ~shepard() {}

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

  YSE::DSP::lowPass lp;

  Flt lpFreq;
  YSE::DSP::buffer s1, s2;
};

shepard::shepard() {
  // shepard tones are created with parallel octaves, so we double
  // the frequency for every generator
  freq[0] = 10;
  for (UInt i = 1; i < 11; i++) {
    freq[i] = freq[i - 1] * 2;
  }
  // the maximum frequency that can be reached
  top = freq[10] * 2;

  lp.setFrequency(1000);
  lpFreq = 500;

}

void shepard::process(YSE::SOUND_STATUS & intent) {
  // first clear the output buffer
  out = 0.0f;
  //out += generators[5](freq[5]);
  // add all sine generators to the output
  for (UInt i = 0; i < 11; i++) {
    out += generators[i](freq[i]);

    // adjust frequency for next run
    freq[i] = YSE::DSP::MidiToFreq(YSE::DSP::FreqToMidi(freq[i]) + 0.02f);
    // back down at maximum frequency
    if (freq[i] > top) freq[i] = 10;
  }
  // scale output volume
  out *= 0.1f;

  // most DSP object will return a reference to an AUDIOBUFFER.
  YSE::DSP::buffer & result = lp(out);

  // if you need to alter the result afterwards, you should not use a reference but
  // AUDIOBUFFER result = lp(out);
  // Note that this makes a deep copy of the object output, so use only when really needed
  // and preferably create the sample object when setting up your dsp object. Creating a
  // new sample in the process function will require memory allocation every time it runs.

  // copy buffer to all channels (YSE creates the buffer vector for your dsp, according to 
  // the channels chosen for the current output device
  for (UInt i = 0; i < samples.size(); i++) {
    samples[i] = result;
  }

  if (intent == YSE::SS_WANTSTOSTOP) intent = YSE::SS_STOPPED;
}

void shepard::frequency(Flt value) {
  lp.setFrequency(value);
  lpFreq = value;
}

Flt shepard::frequency() {
  return lpFreq;
}

YSE::INTERNAL::AudioTest::AudioTest() {
  shep = new shepard();
  testSound.create(*shep);
}

YSE::INTERNAL::AudioTest::~AudioTest() {
  delete shep;
}

void YSE::INTERNAL::AudioTest::On(bool value) {
  if (value) {
    testSound.play();
  }
  else {
    testSound.stop();
  }
}