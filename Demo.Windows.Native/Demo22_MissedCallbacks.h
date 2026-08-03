
#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

// Minimal engine-only harness for issue #406. On start it just opens the audio
// device and idles; it then reports the missed-callback count on a steady
// interval. A sine wave and a sample can each be toggled independently so we
// can tell whether the reported drops are tied to any audio activity or happen
// even with a completely idle device.
class DemoMissedCallbacks : public basePage {
public:
  DemoMissedCallbacks();
  ~DemoMissedCallbacks();

  virtual void ExplainDemo();
  virtual void ShowStatus();

  void ToggleSine();
  void ToggleSample();

private:
  // A single built-in sine source (cf. shepard in Demo07_DspSource). Declared
  // before the sound so it outlives it: members destruct in reverse order.
  YSE::DSP::sineWave sine_;
  YSE::sound sineSound_;
  YSE::sound sampleSound_;
  bool sampleValid_ = false;

  // Cumulative latch. system::update() resets missedCallbacks() to 0 as soon as
  // a control tick sees callbacks again (system.cpp:112-121, :167), so the
  // getter is a consecutive-missed-ticks gauge, not a running total. We bump
  // cumulativeMissed_ on every 0 -> non-zero transition so intermittent drops
  // stay visible over the session, and track the largest run in peakMissed_.
  int previousMissed_ = 0;
  unsigned long long cumulativeMissed_ = 0;
  int peakMissed_ = 0;

  // Throttle the on-screen refresh to ~once per second. basePage::Run() calls
  // ShowStatus() every ~100 ms tick (it sleeps 100 ms), so latch every tick but
  // only redraw every tenth.
  int refreshCounter_ = 0;
  int seconds_ = 0;
};
