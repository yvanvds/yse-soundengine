#include "stdafx.h"
#include "Demo07_DspSource.h"


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

}

void shepard::frequency(Flt value) {
  lp.setFrequency(value);
  lpFreq = value;
}

Flt shepard::frequency() {
  return lpFreq;
}


DemoDspSource::DemoDspSource()
{
  SetTitle("Dsp Source Demo.");
  AddAction('1', "Decrease lowpass filter frequency.", std::bind(&DemoDspSource::FilterDown, this));
  AddAction('2', "Increase lowpass filter frequency.", std::bind(&DemoDspSource::FilterUp, this));

  sound.create(s).play();
}


DemoDspSource::~DemoDspSource()
{
}

void DemoDspSource::ExplainDemo()
{
  std::cout << "A DSP source object is used here instead of a sample. The object contains a" << std::endl;
  std::cout << "lowpass filter which can be adjusted." << std::endl;
}

void DemoDspSource::FilterUp()
{
  s.frequency(s.frequency() + 50);
}

void DemoDspSource::FilterDown()
{
  s.frequency(s.frequency() - 50);
}
