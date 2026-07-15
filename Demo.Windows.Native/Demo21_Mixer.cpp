#include "stdafx.h"

#include "Demo21_Mixer.h"

#include <iostream>

DemoMixer::DemoMixer() {
  SetTitle("Mix (insert chain + send/return reverb)");

  AddAction('1', "Toggle music insert chain (EQ/comp/chorus)",
            std::bind(&DemoMixer::ToggleInserts, this));
  AddAction('q', "Reverb send up", std::bind(&DemoMixer::SendUp, this));
  AddAction('a', "Reverb send down", std::bind(&DemoMixer::SendDown, this));
  AddAction('w', "Music volume up", std::bind(&DemoMixer::MusicVolUp, this));
  AddAction('s', "Music volume down", std::bind(&DemoMixer::MusicVolDown, this));
  AddAction('e', "Drum volume up", std::bind(&DemoMixer::DrumVolUp, this));
  AddAction('d', "Drum volume down", std::bind(&DemoMixer::DrumVolDown, this));

  // Two source channels under the master.
  musicCh_.create("music", YSE::ChannelMaster());
  drumCh_.create("drums", YSE::ChannelMaster());

  // Build the music channel's insert chain: EQ -> compressor -> chorus.
  eq_.gain(YSE::DSP::MODULES::EQ_LOW_SHELF, 4.0f);
  eq_.frequency(YSE::DSP::MODULES::EQ_PEAK_1, 900.f).gain(YSE::DSP::MODULES::EQ_PEAK_1, -3.0f);
  eq_.gain(YSE::DSP::MODULES::EQ_HIGH_SHELF, 3.0f);
  comp_.threshold(-18.f).ratio(4.f).attack(10.f).release(120.f).makeup(4.f);
  chorus_.mode(YSE::DSP::MODULES::MODE_CHORUS).rate(0.6f).depth(0.4f).impact(0.4f);
  eq_.link(comp_);
  comp_.link(chorus_);
  musicCh_.setDSP(&eq_);

  // A shared plate-reverb return bus fed post-fader from both channels.
  reverbReturn_.makeReturn("reverb");
  plate_.decay(0.6f).damping(6000.f).preDelay(20.f).impact(1.0f);
  reverbReturn_.setDSP(&plate_);
  musicCh_.send(0, reverbReturn_, sendLevel_);
  drumCh_.send(0, reverbReturn_, sendLevel_);

  // Looping source material from the always-present TestResources.
  musicLoop_ = std::make_unique<YSE::sound>();
  musicLoop_->create(YSE_TEST_RESOURCES_DIR "/pulse1.ogg", &musicCh_, true);
  musicLoop_->play();

  drumLoop_ = std::make_unique<YSE::sound>();
  drumLoop_->create(YSE_TEST_RESOURCES_DIR "/kick.ogg", &drumCh_, true);
  drumLoop_->play();
}

DemoMixer::~DemoMixer() {
  if (musicLoop_) musicLoop_->stop();
  if (drumLoop_) drumLoop_->stop();
  for (int i = 0; i < 5; i++) {
    YSE::System().update();
    YSE::System().sleep(20);
  }
  musicLoop_.reset();
  drumLoop_.reset();
  // Tear down the routing before the channels/effects destruct so the audio
  // thread stops touching the insert chain and sends first.
  musicCh_.clearSend(0);
  drumCh_.clearSend(0);
  musicCh_.setDSP(nullptr);
  reverbReturn_.setDSP(nullptr);
  for (int i = 0; i < 5; i++) {
    YSE::System().update();
    YSE::System().sleep(20);
  }
}

void DemoMixer::ExplainDemo() {
  std::cout << "A console mixer: the music channel runs an EQ -> compressor -> chorus" << std::endl;
  std::cout << "insert chain, and both channels feed a shared plate-reverb send/return."
            << std::endl;
}

void DemoMixer::ShowStatus() {
  std::cout << "\rinserts=" << (insertsOn_ ? "ON " : "OFF") << "  send=" << sendLevel_
            << "  music=" << musicCh_.getVolume() << "  drums=" << drumCh_.getVolume()
            << "  comp GR=" << comp_.gainReductionDb() << " dB        " << std::flush;
}

void DemoMixer::ToggleInserts() {
  insertsOn_ = !insertsOn_;
  musicCh_.setDSP(insertsOn_ ? &eq_ : nullptr);
}

void DemoMixer::SendUp() {
  sendLevel_ = sendLevel_ + 0.1f > 1.0f ? 1.0f : sendLevel_ + 0.1f;
  musicCh_.setSendLevel(0, sendLevel_);
  drumCh_.setSendLevel(0, sendLevel_);
}

void DemoMixer::SendDown() {
  sendLevel_ = sendLevel_ - 0.1f < 0.0f ? 0.0f : sendLevel_ - 0.1f;
  musicCh_.setSendLevel(0, sendLevel_);
  drumCh_.setSendLevel(0, sendLevel_);
}

void DemoMixer::MusicVolUp() {
  musicCh_.setVolume(musicCh_.getVolume() + 0.1f);
}

void DemoMixer::MusicVolDown() {
  musicCh_.setVolume(musicCh_.getVolume() - 0.1f);
}

void DemoMixer::DrumVolUp() {
  drumCh_.setVolume(drumCh_.getVolume() + 0.1f);
}

void DemoMixer::DrumVolDown() {
  drumCh_.setVolume(drumCh_.getVolume() - 0.1f);
}
