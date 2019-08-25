#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

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