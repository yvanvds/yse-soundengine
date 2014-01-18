#include <iostream>
#include <cstdlib>
#include "yse.hpp"
#ifdef WINDOWS
#include <conio.h>
#else
#include "wincompat.h"
#endif


// inherit your dsp class from dspSource
class shepard : public YSE::DSP::dspSource {
public:
  // process function is pure virtual in dspSource
  // you HAVE to implement it
  virtual void process(YSE::SOUND_STATUS & intent, Int & latency);
  // constructor can be implement if you need it
  // (you probably will)
  shepard();
  void frequency(Flt value);
  Flt frequency();

private:
  // in this case we add:
  // a sample buffer to hold the sum of all generators
  YSE::DSP::sample out;
  // sinewave generators
  YSE::DSP::sine generators[11];
  // frequencies for all generators
  Flt freq[11];
  // the maximum frequency
  Flt top;

  YSE::DSP::lowPass lp;

  Flt lpFreq;
  YSE::DSP::sample s1, s2;
};

shepard::shepard() {
  // shepard tones are created with parallel octaves, so we double
  // the frequency for every generator
  freq[0] = 10;
  for (UInt i = 1; i < 11; i++) {
    freq[i] = freq[i-1] * 2;
  }
  // the maximum frequency that can be reached
  top = freq[10] * 2;

  lp.setFrequency(1000);
  lpFreq = 500;
  
}

void shepard::process(YSE::SOUND_STATUS & intent, Int & latency) {
  // first clear the output buffer
  out = 0.0f;
  // add all sine generators to the output
  for (UInt i = 0; i < 11; i++) {
    out += generators[i](freq[i]);

    // adjust frequency for next run
    freq[i] = YSE::DSP::MidiToFreq(YSE::DSP::FreqToMidi(freq[i]) + 0.01);
    // back down at maximum frequency
    if (freq[i] > top) freq[i] = 10; 
  }
  // scale output
  out *= 0.6f;
  
  // most DSP object will return a const reference to a sample, called SAMPLE.
  // you can use the define SAMPLE to create a const reference, like this:
  SAMPLE result = lp(out);
  // if you need to alter the result afterwards, use a normal sample, like
  // YSE::DSP::sample result = lp(out);
  // Note that this makes a deep copy of the object output, so use only when really needed
  // and preferably create the sample object when setting up your dsp object. Creating a
  // new sample in the process function would require memory allocation every time it runs.
  

  // copy buffer to all channels (YSE creates the buffer vector for your dsp, according to 
  // the channels chosen for the current output device
	for (UInt i = 0; i < buffer.size(); i++) {
		buffer[i] = result;
	}

}

void shepard::frequency(Flt value) {
  lp.setFrequency(value);
  lpFreq = value;
}

Flt shepard::frequency() {
  return lpFreq;
}


int main() {
  YSE::System.init();

  // create your dsp object and add it to the system
  shepard s;
  YSE::sound sound;
  sound.create(s).play();

  std::cout << " e to exit."  << std::endl;
  while (true) {
    if (_kbhit()) {
      char ch = _getch();
      switch(ch) {
        case 'e': goto exit;
        case '1': s.frequency(s.frequency() - 50); std::cout << s.frequency() << std::endl; break;
        case '2': s.frequency(s.frequency() + 50); std::cout << s.frequency() << std::endl; break;
      }
    }

    YSE::System.sleep (100);
    YSE::System.update(   );
  }    

exit:
  YSE::System.close();
  return 0;
}
