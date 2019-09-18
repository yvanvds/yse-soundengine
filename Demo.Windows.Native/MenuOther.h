
#pragma once

#include "basePage.h"

class OtherMenu : public basePage
{
public:
	OtherMenu();
  
	void DevicesDemo();
	void VirtualIODemo();
	void AudioTestDemo();
	void RestartAudioDemo();
	void MidiDemo();
	void MidiPatcherDemo();
	void PitchTest();
};

