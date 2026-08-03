
#include "stdafx.h"

#include "Demo22_MissedCallbacks.h"

#include <iostream>

DemoMissedCallbacks::DemoMissedCallbacks() {
  SetTitle("Missed Callbacks (idle drift harness)");

  AddAction('1', "Toggle sine wave", std::bind(&DemoMissedCallbacks::ToggleSine, this));
  AddAction('2', "Toggle sample", std::bind(&DemoMissedCallbacks::ToggleSample, this));

  // Prepare a sine source and a looping sample, but start both silent: the
  // point of the demo is to watch the device at idle first.
  sine_.frequency(220.f);
  sineSound_.create(sine_);

  sampleSound_.create(YSE_TEST_RESOURCES_DIR "/drone.ogg", nullptr, true);
  sampleValid_ = sampleSound_.isValid();
  if (!sampleValid_) {
    std::cout << "sample 'drone.ogg' not found - sample toggle disabled" << std::endl;
  }
}

DemoMissedCallbacks::~DemoMissedCallbacks() {
  // Stop both sounds and let the engine's slow-pool delete tick fire before the
  // sine source (sine_) destructs, so the audio thread stops calling into it
  // (see the lifetime contract on sound::create(dspSourceObject&)).
  sineSound_.stop();
  if (sampleValid_) sampleSound_.stop();
  for (int i = 0; i < 5; i++) {
    YSE::System().update();
    YSE::System().sleep(20);
  }
  std::cout << std::endl;
}

void DemoMissedCallbacks::ExplainDemo() {
  std::cout << "Opens the audio device and idles. The line below refreshes about once a"
            << std::endl;
  std::cout << "second with the instantaneous missedCallbacks() value plus a cumulative"
            << std::endl;
  std::cout << "count of every 0 -> non-zero transition over the session (the engine getter"
            << std::endl;
  std::cout << "resets itself, so the running total is latched here). Toggle a sine wave (1)"
            << std::endl;
  std::cout << "and/or a sample (2) to see whether drops track audio activity." << std::endl;
}

void DemoMissedCallbacks::ShowStatus() {
  // Sample every tick (~100 ms) so no 0 -> non-zero transition is missed: the
  // engine clears the counter on the next control tick that sees callbacks.
  int now = YSE::System().missedCallbacks();
  if (previousMissed_ == 0 && now > 0) cumulativeMissed_++;
  if (now > peakMissed_) peakMissed_ = now;
  previousMissed_ = now;

  // Redraw roughly once a second (10 ticks * ~100 ms).
  if (++refreshCounter_ < 10) return;
  refreshCounter_ = 0;
  seconds_++;

  std::cout << "\r[" << seconds_ << "s] missedCallbacks(now)=" << now
            << "  cumulative=" << cumulativeMissed_ << "  peak=" << peakMissed_
            << "  sine=" << (sineSound_.isPlaying() ? "on " : "off")
            << "  sample=" << (sampleSound_.isPlaying() ? "on " : "off") << "        "
            << std::flush;
}

void DemoMissedCallbacks::ToggleSine() {
  sineSound_.toggle();
}

void DemoMissedCallbacks::ToggleSample() {
  if (!sampleValid_) return;
  sampleSound_.toggle();
}
