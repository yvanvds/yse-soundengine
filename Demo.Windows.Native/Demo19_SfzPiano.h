#pragma once

#include <memory>

#include "basePage.h"
#include "../YseEngine/yse.hpp"
#include "../YseEngine/synth/samplerVoice.hpp"

#if YSE_ENABLE_MIDI_DEVICE
#include "../YseEngine/midi/device.hpp"
#endif

// Demo19 — SFZ piano (issue #180, epic #145/#149).
//
// The bundled SFZ instrument played from a MIDI keyboard or the console note
// keys, with the sustain pedal (CC 64) demonstrated: while sustain is down,
// released keys keep ringing until the pedal lifts. The seed content pack ships
// a small CC0 SFZ (content/sfz/yse_pulse.sfz); the opt-in fetch path can drop a
// full Salamander/VSCO piano at the same location. Without the content pack the
// page prints a clear message and offers nothing to play.
class DemoSfzPiano : public basePage {
public:
  DemoSfzPiano();
  ~DemoSfzPiano();

  void ExplainDemo() override;

private:
  void ListMidiPorts();
  void OpenMidiPort();
  void PlayChord();
  void ReleaseChord();
  void ToggleSustain();
  void AllNotesOff();

  bool available_ = false;
  bool sustainDown_ = false;

  std::unique_ptr<YSE::SYNTH::samplerVoice> proto_;
  std::unique_ptr<YSE::synth> synth_;
  std::unique_ptr<YSE::sound> sound_;

#if YSE_ENABLE_MIDI_DEVICE
  std::unique_ptr<YSE::midiIn> midi_;
  bool midiConnected_ = false;
#endif
};
