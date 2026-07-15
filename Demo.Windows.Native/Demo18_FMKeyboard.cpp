#include "stdafx.h"

#include "Demo18_FMKeyboard.h"

#include <fstream>
#include <iostream>
#include <string>

namespace {
  // The bundled CC0 FM bank shipped with the content pack (issue #179).
  const char* kBankPath = YSE_CONTENT_PACK_DIR "/fm/original/yse_originals.syx";

  // A C-major triad, used to audition the current patch without a keyboard.
  constexpr int kChord[3] = {60, 64, 67};
  constexpr int kMidiChannel = 1;

  bool fileExists(const char* path) {
    std::ifstream f(path, std::ios::binary);
    return f.good();
  }
} // namespace

DemoFMKeyboard::DemoFMKeyboard() {
  SetTitle("FM + MIDI Keyboard");

  AddAction('1', "List MIDI ports", std::bind(&DemoFMKeyboard::ListMidiPorts, this));
  AddAction('2', "Open first MIDI input port", std::bind(&DemoFMKeyboard::OpenMidiPort, this));
  AddAction('q', "Next patch", std::bind(&DemoFMKeyboard::NextPatch, this));
  AddAction('a', "Previous patch", std::bind(&DemoFMKeyboard::PrevPatch, this));
  AddAction('z', "Play/hold C-major chord", std::bind(&DemoFMKeyboard::PlayChord, this));
  AddAction('x', "Release C-major chord", std::bind(&DemoFMKeyboard::ReleaseChord, this));
  AddAction('c', "All notes off", std::bind(&DemoFMKeyboard::AllNotesOff, this));

  if (!fileExists(kBankPath)) {
    std::cout << "The bundled FM bank was not found at:\n  " << kBankPath << "\n"
              << "Build with the content pack present (issue #179) to run this demo." << std::endl;
    return;
  }

  if (!YSE::SYNTH::dx7SysEx::loadBank(kBankPath, bank_) || bank_.empty()) {
    std::cout << "Failed to parse the bundled DX7 bank at:\n  " << kBankPath << std::endl;
    return;
  }

  // Build a 16-voice FM synth from the first bank patch and attach it behind a
  // positioned sound. The prototype must outlive addVoices (it is a member).
  proto_ = std::make_unique<YSE::SYNTH::fmVoice>();
  proto_->setPatch(bank_.voices[patchIndex_]);

  synth_ = std::make_unique<YSE::synth>();
  synth_->create().addVoices(*proto_, 16);

  sound_ = std::make_unique<YSE::sound>();
  sound_->create(*synth_);
  sound_->play();

  available_ = true;

#if YSE_ENABLE_MIDI_DEVICE
  midi_ = std::make_unique<YSE::midiIn>();
  // Auto-connect the first available MIDI input so a plugged-in keyboard just
  // works; the console note keys remain a hardware-free fallback.
  if (YSE::System().getNumMidiInDevices() > 0) {
    midi_->create(0);
    midi_->connect(*synth_); // omni
    midiConnected_ = true;
  }
#endif

  std::cout << "Loaded FM bank with " << bank_.size() << " patches. Current: 0 (" << bank_.name(0)
            << ")." << std::endl;
}

DemoFMKeyboard::~DemoFMKeyboard() {
#if YSE_ENABLE_MIDI_DEVICE
  if (midi_ && synth_ && midiConnected_) midi_->disconnect(*synth_);
  midi_.reset();
#endif
  if (synth_) synth_->allNotesOff();
  if (sound_) sound_->stop();
  // Let the sound release and the slow pool reclaim it before the synth (and
  // then the prototype) go away, honouring the synth lifetime contract.
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

void DemoFMKeyboard::ExplainDemo() {
  std::cout << "A 6-operator FM synth (DX7-class) driven by a MIDI keyboard or the" << std::endl;
  std::cout << "console note keys. Patches come from the bundled DX7 SysEx bank." << std::endl;
  if (!available_) {
    std::cout << "\n[content pack absent — nothing to play]" << std::endl;
  }
}

void DemoFMKeyboard::ShowStatus() {}

void DemoFMKeyboard::ListMidiPorts() {
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

void DemoFMKeyboard::OpenMidiPort() {
#if YSE_ENABLE_MIDI_DEVICE
  if (!available_) {
    std::cout << "Synth unavailable — cannot connect MIDI." << std::endl;
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
  std::cout << "Connected MIDI port 0 to the FM synth (omni)." << std::endl;
#else
  std::cout << "MIDI device backend disabled at build time (YSE_ENABLE_MIDI_DEVICE=OFF)."
            << std::endl;
#endif
}

void DemoFMKeyboard::applyPatch() {
  if (!available_) return;
  proto_->setPatch(bank_.voices[patchIndex_]);
  // Retrigger held notes so the new patch is audible immediately.
  synth_->allNotesOff();
  chordDown_ = false;
  std::cout << "Patch " << patchIndex_ << ": " << bank_.name(patchIndex_) << std::endl;
}

void DemoFMKeyboard::NextPatch() {
  if (!available_) return;
  patchIndex_ = (patchIndex_ + 1) % static_cast<int>(bank_.size());
  applyPatch();
}

void DemoFMKeyboard::PrevPatch() {
  if (!available_) return;
  patchIndex_ = (patchIndex_ + static_cast<int>(bank_.size()) - 1) % static_cast<int>(bank_.size());
  applyPatch();
}

void DemoFMKeyboard::PlayChord() {
  if (!available_) return;
  for (int n : kChord)
    synth_->noteOn(kMidiChannel, n, 0.8f);
  chordDown_ = true;
}

void DemoFMKeyboard::ReleaseChord() {
  if (!available_) return;
  for (int n : kChord)
    synth_->noteOff(kMidiChannel, n);
  chordDown_ = false;
}

void DemoFMKeyboard::AllNotesOff() {
  if (!available_) return;
  synth_->allNotesOff();
  chordDown_ = false;
}
