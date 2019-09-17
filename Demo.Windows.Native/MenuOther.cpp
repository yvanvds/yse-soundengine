
#include "stdafx.h"

#include "MenuOther.h"
#include "Demo06_Devices.h"
#include "Demo11_VirtualIO.h"
#include "Demo12_AudioTest.h"
#include "Demo15_RestartAudio.h"
#include "Test01_Pitch.h"

OtherMenu::OtherMenu()
{
	SetTitle("Other Examples");
	AddAction('1', "Devices", std::bind(&OtherMenu::DevicesDemo, this));
	AddAction('2', "Virtual IO", std::bind(&OtherMenu::VirtualIODemo, this));
	AddAction('3', "Audio Test", std::bind(&OtherMenu::AudioTestDemo, this));
	AddAction('4', "Restart Audio", std::bind(&OtherMenu::RestartAudioDemo, this));
	AddAction('5', "Test Pitch", std::bind(&OtherMenu::PitchTest, this));

}

void OtherMenu::DevicesDemo()
{
  DemoDevices demo;
  demo.Run();
  ShowMenu();
}

void OtherMenu::VirtualIODemo() {
  DemoVirtualIO demo;
  demo.Run();
  ShowMenu();
}

void OtherMenu::AudioTestDemo() {
  DemoAudioTest demo;
  demo.Run();
  ShowMenu();
}

void OtherMenu::RestartAudioDemo() {
	DemoRestartAudio demo;
	demo.Run();
	ShowMenu();
}

void OtherMenu::PitchTest() {
	Test01_Pitch demo;
	demo.Run();
	ShowMenu();
}