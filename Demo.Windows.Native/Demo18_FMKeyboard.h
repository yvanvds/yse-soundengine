#pragma once

#include <memory>

#include "basePage.h"
#include "../YseEngine/yse.hpp"
#include "../YseEngine/dsp/fm/fmVoice.hpp"
#include "../YseEngine/dsp/fm/dx7Sysex.hpp"

#if YSE_ENABLE_MIDI_DEVICE
#include "../YseEngine/midi/device.hpp"
#endif

// Demo18 — FM + MIDI (issue #180, epic #145/#149).
//
// A hardware MIDI keyboard drives a 6-operator DX7-class FM synth loaded from
// the bundled DX7 SysEx bank (content/fm/original/yse_originals.syx). Patches
// are switched from the console, extending the Demo16/17 MIDI lineage into the
// synth engine. Without the content pack the page prints a clear message and
// offers nothing to play; without a MIDI device the console note keys still
// exercise the whole FM path.
class DemoFMKeyboard : public basePage {
public:
  DemoFMKeyboard();
  ~DemoFMKeyboard();

  void ExplainDemo() override;
  void ShowStatus() override;

private:
  void ListMidiPorts();
  void OpenMidiPort();
  void NextPatch();
  void PrevPatch();
  void PlayChord();
  void ReleaseChord();
  void AllNotesOff();
  void applyPatch();

  bool available_ = false; // content pack present and synth built
  bool chordDown_ = false;

  YSE::SYNTH::dx7Bank bank_;
  int patchIndex_ = 0;

  std::unique_ptr<YSE::SYNTH::fmVoice> proto_;
  std::unique_ptr<YSE::synth> synth_;
  std::unique_ptr<YSE::sound> sound_;

#if YSE_ENABLE_MIDI_DEVICE
  std::unique_ptr<YSE::midiIn> midi_;
  bool midiConnected_ = false;
#endif
};
