#include "stdafx.h"
#include "MenuOther.h"
#include "Demo06_Devices.h"
#include "Demo11_VirtualIO.h"
#include "Demo12_AudioTest.h"

OtherMenu::OtherMenu()
{
  SetTitle("Other Examples");
  AddAction('1', "Devices", std::bind(&OtherMenu::DevicesDemo, this));
  AddAction('2', "Virtual IO", std::bind(&OtherMenu::VirtualIODemo, this));
  AddAction('3', "Audio Test", std::bind(&OtherMenu::AudioTestDemo, this));
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
