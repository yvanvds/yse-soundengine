#include "stdafx.h"

#include "Demo19_SfzPiano.h"

#include <fstream>
#include <iostream>
#include <string>

namespace {
  // The bundled CC0 SFZ instrument shipped with the content pack (issue #179).
  // The opt-in fetch path may replace this with a full Salamander/VSCO piano at
  // the same location; this demo plays whatever instrument is present.
  const char* kSfzPath = YSE_CONTENT_PACK_DIR "/sfz/yse_pulse.sfz";

  constexpr int kChord[3] = {60, 64, 67};
  constexpr int kMidiChannel = 1;

  bool fileExists(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
  }
} // namespace

DemoSfzPiano::DemoSfzPiano() {
  SetTitle("SFZ Piano");

  AddAction('1', "List MIDI ports", std::bind(&DemoSfzPiano::ListMidiPorts, this));
  AddAction('2', "Open first MIDI input port", std::bind(&DemoSfzPiano::OpenMidiPort, this));
  AddAction('z', "Play C-major chord", std::bind(&DemoSfzPiano::PlayChord, this));
  AddAction('x', "Release C-major chord", std::bind(&DemoSfzPiano::ReleaseChord, this));
  AddAction('s', "Toggle sustain pedal (CC 64)", std::bind(&DemoSfzPiano::ToggleSustain, this));
  AddAction('c', "All notes off", std::bind(&DemoSfzPiano::AllNotesOff, this));

  if (!fileExists(kSfzPath)) {
    std::cout << "The bundled SFZ instrument was not found at:\n  " << kSfzPath << "\n"
              << "Build with the content pack present (issue #179) to run this demo." << std::endl;
    return;
  }

  proto_ = std::make_unique<YSE::SYNTH::samplerVoice>();
  if (!proto_->loadSFZ(kSfzPath)) {
    std::cout << "Failed to load the SFZ instrument at:\n  " << kSfzPath << std::endl;
    proto_.reset();
    return;
  }

  synth_ = std::make_unique<YSE::synth>();
  synth_->create().addVoices(*proto_, 24);

  sound_ = std::make_unique<YSE::sound>();
  sound_->create(*synth_);
  sound_->play();

  available_ = true;

#if YSE_ENABLE_MIDI_DEVICE
  midi_ = std::make_unique<YSE::midiIn>();
  if (YSE::System().getNumMidiInDevices() > 0) {
    midi_->create(0);
    midi_->connect(*synth_); // omni
    midiConnected_ = true;
  }
#endif

  std::cout << "SFZ instrument loaded and ready to play." << std::endl;
}

DemoSfzPiano::~DemoSfzPiano() {
#if YSE_ENABLE_MIDI_DEVICE
  if (midi_ && synth_ && midiConnected_) midi_->disconnect(*synth_);
  midi_.reset();
#endif
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
  proto_.reset();
}

void DemoSfzPiano::ExplainDemo() {
  std::cout << "A bundled SFZ instrument played from a MIDI keyboard or the console" << std::endl;
  std::cout << "note keys. Hold the sustain pedal (CC 64) and release keys — the notes"
            << std::endl;
  std::cout << "keep ringing until the pedal lifts." << std::endl;
  if (!available_) {
    std::cout << "\n[content pack absent — nothing to play]" << std::endl;
  }
}

void DemoSfzPiano::ListMidiPorts() {
#if YSE_ENABLE_MIDI_DEVICE
  int in = YSE::System().getNumMidiInDevices();
  std::cout << "MIDI input ports: " << in << std::endl;
  for (int i = 0; i < in; i++) {
    std::cout << "  " << i << ": " << YSE::System().getMidiInDeviceName(i) << std::endl;
  }
#else
  std::cout << "MIDI device backend disabled at build time (YSE_ENABLE_MIDI_DEVICE=OFF)."
            << std::endl;
#endif
}

void DemoSfzPiano::OpenMidiPort() {
#if YSE_ENABLE_MIDI_DEVICE
  if (!available_) {
    std::cout << "Instrument unavailable — cannot connect MIDI." << std::endl;
    return;
  }
  if (YSE::System().getNumMidiInDevices() == 0) {
    std::cout << "No MIDI input ports available." << std::endl;
    return;
  }
  if (midiConnected_) midi_->disconnect(*synth_);
  midi_->close();
  midi_->create(0);
  midi_->connect(*synth_); // omni
  midiConnected_ = true;
  std::cout << "Connected MIDI port 0 to the SFZ instrument (omni)." << std::endl;
#else
  std::cout << "MIDI device backend disabled at build time (YSE_ENABLE_MIDI_DEVICE=OFF)."
            << std::endl;
#endif
}

void DemoSfzPiano::PlayChord() {
  if (!available_) return;
  for (int n : kChord)
    synth_->noteOn(kMidiChannel, n, 0.8f);
}

void DemoSfzPiano::ReleaseChord() {
  if (!available_) return;
  // With sustain down these note-offs are deferred until the pedal lifts.
  for (int n : kChord)
    synth_->noteOff(kMidiChannel, n);
}

void DemoSfzPiano::ToggleSustain() {
  if (!available_) return;
  sustainDown_ = !sustainDown_;
  synth_->sustain(kMidiChannel, sustainDown_);
  std::cout << "Sustain pedal " << (sustainDown_ ? "DOWN" : "UP") << "." << std::endl;
}

void DemoSfzPiano::AllNotesOff() {
  if (!available_) return;
  synth_->allNotesOff();
}
