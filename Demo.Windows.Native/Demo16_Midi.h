#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

// The DemoMidi page exercises the RtMidi-backed device backend. When libyse
// is built with YSE_ENABLE_MIDI_DEVICE=OFF the underlying YSE::midiOut type
// is not declared, so this whole class compiles out. MenuOther guards the
// matching `MidiDemo()` call site.
#if YSE_ENABLE_MIDI_DEVICE
class DemoMidi : public basePage {
public:
	DemoMidi();
	~DemoMidi();

	void PrintMidiPorts();
	void PlayNote();
	void AllNotesOff();

	YSE::midiOut output1;
	YSE::midiOut output2;
	bool firstNote;
};
#endif