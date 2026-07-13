#pragma once

#include <memory>

#include "basePage.h"
#include "../YseEngine/yse.hpp"
#include "../YseEngine/dsp/modules/parametricEQ.hpp"
#include "../YseEngine/dsp/modules/compressor.hpp"
#include "../YseEngine/dsp/modules/chorus.hpp"
#include "../YseEngine/dsp/modules/plateReverb.hpp"

// Demo21 — Mix (issue #180, epic #146/#147 showcase).
//
// A miniature DAW mixer session in the console: two source channels (a music
// loop and a drum loop) with a pre-fader insert chain (parametric EQ ->
// compressor -> chorus) on the music channel, plus a shared send/return plate
// reverb bus fed post-fader from both channels. The console adjusts send levels,
// channel faders, and bypasses the insert chain, while the compressor's live
// gain reduction is metered. Uses the always-present TestResources loops, so it
// runs without the content pack.
class DemoMixer : public basePage {
public:
  DemoMixer();
  ~DemoMixer();

  void ExplainDemo() override;
  void ShowStatus() override;

private:
  void ToggleInserts();
  void SendUp();
  void SendDown();
  void MusicVolUp();
  void MusicVolDown();
  void DrumVolUp();
  void DrumVolDown();

  bool insertsOn_ = true;
  float sendLevel_ = 0.3f;

  // Effects — declared first so they outlive the channels that reference them.
  YSE::DSP::MODULES::parametricEQ eq_;
  YSE::DSP::MODULES::compressor comp_;
  YSE::DSP::MODULES::chorus chorus_;
  YSE::DSP::MODULES::plateReverb plate_;

  // Channels — destroyed before the effects, after the sounds.
  YSE::channel musicCh_;
  YSE::channel drumCh_;
  YSE::channel reverbReturn_;

  // Sounds — declared last so they are destroyed first.
  std::unique_ptr<YSE::sound> musicLoop_;
  std::unique_ptr<YSE::sound> drumLoop_;
};
