#pragma once

#include "basePage.h"
#include "../YseEngine/yse.hpp"

class DemoMidiPatcher : public basePage {
public:	
	DemoMidiPatcher();

	void NoteOn();
	void NoteOff();
	void AllNotesOff();

private :
	void Setup();

	YSE::sound sound;
	YSE::patcher patcher;

	YSE::pHandle* controlNoteOn;
	YSE::pHandle* controlNoteOff;

	YSE::pHandle* controlVelocityOn;
	YSE::pHandle* allNotesOff;
	YSE::pHandle* controlAllOff;


	YSE::pHandle* noteOff;
	YSE::pHandle* noteOn;
	YSE::pHandle* midiOut;

	int currentNote = 0;
};