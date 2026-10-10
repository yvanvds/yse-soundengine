#include "AudioTest.h"

#include "../internalHeaders.h"

YSE::INTERNAL::AudioTest& YSE::INTERNAL::Test() {
  static AudioTest s;
  return s;
}

// inherit your dsp class from dspSource
class shepard : public YSE::DSP::dspSourceObject {
public:
  // process function is pure virtual in dspSource
  // you HAVE to implement it
  void process(YSE::SOUND_STATUS& intent) override;
  // constructor can be implement if you need it
  // (you probably will)
  shepard();
  void frequency(Flt value) override;
  Flt frequency();

  ~shepard() override {}

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

void shepard::process(YSE::SOUND_STATUS& intent) {
  // first clear the output buffer
  out = 0.0f;
  // out += generators[5](freq[5]);
  //  add all sine generators to the output
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
  YSE::DSP::buffer& result = lp(out);

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
  ensureSound();
}

YSE::INTERNAL::AudioTest::~AudioTest() {
  delete shep;
}

// (Re)build the diagnostic sound for the current engine session.
//
// Same shape as the underwater driver's reverb zone (issue #715). Test() is a
// function-local static, so this driver and its `sound` interface are built
// once per process — but the interface's *implementation* is session state:
// SOUND::Manager().destroy() clears every sound implementation at
// System::close(), and each implementation's destructor nulls its interface's
// pimpl. Nothing re-created this one, because it was only ever built in the
// constructor above.
//
// The sound interface's methods are all no-ops while isValid() is false, so
// the symptom was silence rather than a fault: System().AudioTest(true) — and
// the C API's yse_system_audio_test() with it — did nothing at all in every
// session after the first close(). The engine's built-in output diagnostic
// being a silent no-op is precisely the failure it exists to rule out (issue
// #717; the #570 no-op was the same symptom for a different reason).
//
// Re-created on the existing interface rather than as a fresh object, unlike
// the underwater zone: sound::create() asserts on a live pimpl and the
// implementation's destructor has already nulled it, and there is no cached
// setter state to lose — the driver only ever calls play() / stop(). The
// shepard source is owned for the whole process and is deliberately reused, so
// the tone resumes with its generator state intact.
//
// Control thread only — the caller is the public system::AudioTest() entry
// point — so the allocation is off every audio path.
bool YSE::INTERNAL::AudioTest::ensureSound() {
  if (testSound.isValid()) return true;
  // Nothing to attach an implementation to before init() or after close().
  if (!Global().isActive()) return false;
  testSound.create(*shep);
  return testSound.isValid();
}

YSE::sound& YSE::INTERNAL::AudioTest::source() {
  return testSound;
}

void YSE::INTERNAL::AudioTest::On(bool value) {
  if (!ensureSound()) return;
  if (value) {
    testSound.play();
  } else {
    testSound.stop();
  }
}