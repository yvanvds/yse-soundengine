#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

class DemoMidi : public basePage {
public:
	DemoMidi();

	void PrintMidiPorts();
	void PlayNote();

	YSE::MIDI::midiNote note;
	bool firstNote;
};