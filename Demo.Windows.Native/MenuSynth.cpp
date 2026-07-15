
#include "stdafx.h"

#include "MenuSynth.h"
#include "Demo18_FMKeyboard.h"
#include "Demo19_SfzPiano.h"
#include "Demo20_Swarm.h"
#include "Demo21_Mixer.h"

MenuSynth::MenuSynth() {
  SetTitle("Synth & Effects Examples");
  AddAction('1', "FM + MIDI keyboard", std::bind(&MenuSynth::FMKeyboardDemo, this));
  AddAction('2', "SFZ piano", std::bind(&MenuSynth::SfzPianoDemo, this));
  AddAction('3', "Swarm (orbiting notes)", std::bind(&MenuSynth::SwarmDemo, this));
  AddAction('4', "Mix (insert chain + send/return reverb)", std::bind(&MenuSynth::MixerDemo, this));
}

void MenuSynth::FMKeyboardDemo() {
  DemoFMKeyboard demo;
  demo.Run();
  ShowMenu();
}

void MenuSynth::SfzPianoDemo() {
  DemoSfzPiano demo;
  demo.Run();
  ShowMenu();
}

void MenuSynth::SwarmDemo() {
  DemoSwarm demo;
  demo.Run();
  ShowMenu();
}

void MenuSynth::MixerDemo() {
  DemoMixer demo;
  demo.Run();
  ShowMenu();
}
