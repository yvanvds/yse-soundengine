#include "stdafx.h"
#include "Demo03_Virtual.h"
#include <cstdlib>
#include <conio.h>


DemoVirtual::DemoVirtual()
{
	YSE::Log().setHandler(this);
	YSE::System().maxSounds(100);

	SetTitle("Virtual Sounds");
	AddAction('1', "Add 10 sounds", std::bind(&DemoVirtual::AddSound, this));
}

DemoVirtual::~DemoVirtual()
{
	YSE::Log().setHandler(nullptr);
}



void DemoVirtual::ExplainDemo()
{
	std::cout << "Virtualization allows you to add lots of sound to a scene. Only the sounds nearest to the listener will play." << std::endl;
	std::cout << "Press the spacebar to add 10 sounds at a random position." << std::endl;
}

void DemoVirtual::ShowStatus()
{
	_cprintf_s("Sounds: %d / Audio thread CPU Load: %.2f \r", counter, YSE::System().cpuLoad());
}

void DemoVirtual::AddMessage(const std::string & message)
{
	std::cout << message << std::endl;
}

void DemoVirtual::AddSound()
{
	for (int i = 0; i < 10; i++) {
		sounds.emplace_front();

		switch (YSE::Random(4)) {
		case 0: sounds.front().create("..\\TestResources\\contact.ogg", &YSE::ChannelAmbient(), true); break;
		case 1: sounds.front().create("..\\TestResources\\drone.ogg", &YSE::ChannelVoice(), true); break;
		case 2: sounds.front().create("..\\TestResources\\kick.ogg", &YSE::ChannelMusic(), true); break;
		case 3: sounds.front().create("..\\TestResources\\pulse1.ogg", &YSE::ChannelFX(), true); break;
		}
		if (sounds.front().isValid()) {
			sounds.front().pos(YSE::Pos(YSE::Random(20) - 10.f, YSE::Random(20) - 10.f, YSE::Random(20) - 10.f));
			sounds.front().play().volume(0.1f); // it can get very loud with 100's of sounds
			counter++;
		}
	}
}

