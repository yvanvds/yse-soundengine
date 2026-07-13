#include "stdafx.h"

#include "Demo20_Swarm.h"

#include <iostream>

namespace {
  // A four-note chord spread across an octave, so the swarm has several voices
  // orbiting at once.
  constexpr int kChord[4] = {60, 64, 67, 72};
  constexpr int kMidiChannel = 1;
} // namespace

DemoSwarm::DemoSwarm() {
  SetTitle("Swarm (orbiting notes)");

  AddAction('z', "Play swarm chord", std::bind(&DemoSwarm::PlaySwarm, this));
  AddAction('x', "Stop swarm", std::bind(&DemoSwarm::StopSwarm, this));
  AddAction('q', "Move swarm centre left", std::bind(&DemoSwarm::CenterLeft, this));
  AddAction('w', "Move swarm centre right", std::bind(&DemoSwarm::CenterRight, this));
  AddAction('a', "More aftertouch (widen radius)", std::bind(&DemoSwarm::MoreAftertouch, this));
  AddAction('s', "Less aftertouch (narrow radius)", std::bind(&DemoSwarm::LessAftertouch, this));

  voiceProto_ = std::make_unique<YSE::SYNTH::sineVoice>();
  voiceProto_->attack(0.02f).decay(0.1f).sustain(0.7f).release(0.4f);

  handlerProto_ = std::make_unique<YSE::SYNTH::orbitHandler>();
  handlerProto_->radius(1.0f).velocityRadius(2.0f).aftertouchWiden(1.5f).rate(2.5f).releaseSlow(
      0.5f);

  synth_ = std::make_unique<YSE::synth>();
  synth_->create().addVoices(*voiceProto_, 8).positionHandler(*handlerProto_);

  sound_ = std::make_unique<YSE::sound>();
  sound_->create(*synth_);
  sound_->play();
}

DemoSwarm::~DemoSwarm() {
  if (synth_) synth_->allNotesOff();
  if (sound_) sound_->stop();
  for (int i = 0; i < 5; i++) {
    YSE::System().update();
    YSE::System().sleep(20);
  }
  sound_.reset();
  for (int i = 0; i < 5; i++) {
    YSE::System().update();
    YSE::System().sleep(20);
  }
  synth_.reset();
  handlerProto_.reset();
  voiceProto_.reset();
}

void DemoSwarm::ExplainDemo() {
  std::cout << "A chord of notes orbiting the listener via the swarm position handler."
            << std::endl;
  std::cout << "Steer the swarm centre left/right and raise aftertouch to widen the orbit."
            << std::endl;
}

void DemoSwarm::ShowStatus() {
  if (!playing_) return;
  YSE::Pos p = synth_->getVoicePosition(kMidiChannel, kChord[0]);
  std::cout << "\rcentreX=" << centerX_ << "  aftertouch=" << aftertouch_ << "  voice[" << kChord[0]
            << "] pos=(" << p.x << ", " << p.z << ")        " << std::flush;
}

void DemoSwarm::PlaySwarm() {
  for (int n : kChord)
    synth_->noteOn(kMidiChannel, n, 0.9f);
  playing_ = true;
}

void DemoSwarm::StopSwarm() {
  synth_->allNotesOff();
  playing_ = false;
  std::cout << std::endl;
}

void DemoSwarm::CenterLeft() {
  centerX_ -= 1.0f;
  synth_->handlerParam(YSE::SYNTH::HP_CENTER_X, centerX_);
}

void DemoSwarm::CenterRight() {
  centerX_ += 1.0f;
  synth_->handlerParam(YSE::SYNTH::HP_CENTER_X, centerX_);
}

void DemoSwarm::MoreAftertouch() {
  aftertouch_ = aftertouch_ + 0.2f > 1.0f ? 1.0f : aftertouch_ + 0.2f;
  synth_->aftertouch(kMidiChannel, -1, aftertouch_); // channel-wide
}

void DemoSwarm::LessAftertouch() {
  aftertouch_ = aftertouch_ - 0.2f < 0.0f ? 0.0f : aftertouch_ - 0.2f;
  synth_->aftertouch(kMidiChannel, -1, aftertouch_);
}
