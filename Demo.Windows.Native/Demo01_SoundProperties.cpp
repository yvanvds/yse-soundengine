#include "stdafx.h"
#include "Demo01_SoundProperties.h"
#include <iostream>

/* Sound properties:

In this demo a sound is loaded and some of its properties are changed.

*/

DemoSoundProperties::DemoSoundProperties()
{
	// load a sound in memory
	sound.create("..\\TestResources\\contact.ogg", nullptr, true);

	// false on validation means the sound could not be loaded
	if (!sound.isValid()) {
		std::cout << "sound 'contact.ogg' not found" << std::endl;
		return;
	}
	else {
		SetTitle("Change sound properties");
		AddAction('1', "Increase Volume", std::bind(&DemoSoundProperties::IncVolume, this));
		AddAction('2', "Decrease Volume", std::bind(&DemoSoundProperties::DecVolume, this));
		AddAction('3', "Increase Speed", std::bind(&DemoSoundProperties::IncSpeed, this));
		AddAction('4', "Decrease speed", std::bind(&DemoSoundProperties::DecSpeed, this));
	}

	sound.play();
}

void DemoSoundProperties::IncSpeed()
{
	sound.speed(sound.speed() + 0.1f);
}

void DemoSoundProperties::DecSpeed()
{
	sound.speed(sound.speed() - 0.1f);
}

void DemoSoundProperties::IncVolume()
{
	sound.volume(sound.volume() + 0.1f);
}

void DemoSoundProperties::DecVolume()
{
	sound.volume(sound.volume() - 0.1f);
}

