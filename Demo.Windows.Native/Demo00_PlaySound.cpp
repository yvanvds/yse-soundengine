#include "stdafx.h"
#include "Demo00_PlaySound.h"
#include <iostream>

DemoPlaySound::DemoPlaySound()
{
	// load a sound in memory
	sound.create("..\\TestResources\\drone.ogg", nullptr, true);

	// false on validation means the sound could not be loaded
	if (!sound.isValid()) {
		std::cout << "sound 'drone.ogg' not found" << std::endl;
	}
	else {
		SetTitle("Play A Sound");
		AddAction('1', "Toggle", std::bind(&DemoPlaySound::ToggleSound, this));
		AddAction('2', "Play", std::bind(&DemoPlaySound::PlaySound, this));
		AddAction('3', "Pause", std::bind(&DemoPlaySound::PauseSound, this));
		AddAction('4', "Stop", std::bind(&DemoPlaySound::StopSound, this));
	}
}

void DemoPlaySound::ToggleSound()
{
	sound.toggle();
}

void DemoPlaySound::PlaySound()
{
	sound.play();
}

void DemoPlaySound::StopSound()
{
	sound.stop();
}

void DemoPlaySound::PauseSound()
{
	sound.pause();
}


