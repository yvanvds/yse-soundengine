
#pragma once

#include "basePage.h"

// Synth & effects end-to-end demos (issue #180). Groups the four epic showcase
// scenes: FM + MIDI, SFZ piano, swarm positioning, and the mixer session.
class MenuSynth : public basePage {
public:
  MenuSynth();

  void FMKeyboardDemo();
  void SfzPianoDemo();
  void SwarmDemo();
  void MixerDemo();
};
