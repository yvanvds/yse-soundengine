#include "stdafx.h"
#include "MenuBasics.h"
#include "Demo00_PlaySound.h"
#include "Demo01_SoundProperties.h"
#include "Demo02_3D.h"
#include "Demo03_Virtual.h"
#include "Demo04_Channels.h"
#include "Demo05_Reverb.h"
#include "Demo08_Occlusion.h"
#include "Demo09_Streaming.h"
#include "Demo10_FilePosition.h"

BasicsMenu::BasicsMenu()
{
	SetTitle("Basic Audio Examples");
	AddAction('1', "Play a sound", std::bind(&BasicsMenu::PlaySoundDemo, this));
	AddAction('2', "Sound Properties", std::bind(&BasicsMenu::SoundPropsDemo, this));
	AddAction('3', "3D Movement", std::bind(&BasicsMenu::Demo3DDemo, this));
	AddAction('4', "Virtual Sounds", std::bind(&BasicsMenu::VirtualDemo, this));
  AddAction('5', "Custom Channels", std::bind(&BasicsMenu::ChannelDemo, this));
  AddAction('6', "Reverb", std::bind(&BasicsMenu::ReverbDemo, this));
  AddAction('7', "Sound Occlusion", std::bind(&BasicsMenu::OcclusionDemo, this));
  AddAction('8', "Streaming Sound from Disk", std::bind(&BasicsMenu::StreamingDemo, this));
  AddAction('9', "Change File Position", std::bind(&BasicsMenu::FilePosDemo, this));
}

void BasicsMenu::PlaySoundDemo()
{
	DemoPlaySound demo;
	demo.Run();
	ShowMenu();
}

void BasicsMenu::SoundPropsDemo() {
	DemoSoundProperties demo;
	demo.Run();
	ShowMenu();
}

void BasicsMenu::Demo3DDemo()
{
	Demo3D demo;
	demo.Run();
	ShowMenu();
}

void BasicsMenu::VirtualDemo() {
	DemoVirtual demo;
	demo.Run();
	ShowMenu();
}

void BasicsMenu::ChannelDemo()
{
  DemoChannels demo;
  demo.Run();
  ShowMenu();
}

void BasicsMenu::ReverbDemo()
{
  DemoReverb demo;
  demo.Run();
  ShowMenu();
}

void BasicsMenu::OcclusionDemo()
{
  DemoOcclusion demo;
  demo.Run();
  ShowMenu();
}

void BasicsMenu::StreamingDemo()
{
  DemoStreaming demo;
  demo.Run();
  ShowMenu();
}

void BasicsMenu::FilePosDemo()
{
  DemoFilePosition demo;
  demo.Run();
  ShowMenu();
}
