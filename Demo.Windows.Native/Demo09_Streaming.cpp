#include "stdafx.h"
#include "Demo09_Streaming.h"


DemoStreaming::DemoStreaming()
{
  SetTitle("Streaming Audio From Disk");
  AddAction('1', "Increase Sound Speed", std::bind(&DemoStreaming::SpeedInc, this));
  AddAction('2', "Decrease Sound Speed", std::bind(&DemoStreaming::SpeedDec, this));
  AddAction('3', "Pause Sound", std::bind(&DemoStreaming::Pause, this));
  AddAction('4', "Fade out and Stop", std::bind(&DemoStreaming::Fade, this));
  AddAction('5', "Restart at full Volume", std::bind(&DemoStreaming::Play, this));

  // setting the last parameter to true will enable streaming
  sound.create("..\\TestResources\\pulse1.ogg", nullptr, true, 1.f, true);
  sound.play();
}

void DemoStreaming::ExplainDemo()
{
  std::cout << "This demonstrates the use of streaming sounds from disk instead of loading them into memory before playing." << std::endl;
}

void DemoStreaming::SpeedInc()
{
  sound.speed(sound.speed() + 0.01f);
}

void DemoStreaming::SpeedDec()
{
  sound.speed(sound.speed() - 0.01f);
}

void DemoStreaming::Pause()
{
  sound.pause();
}

void DemoStreaming::Fade()
{
  sound.fadeAndStop(3000);
}

void DemoStreaming::Play()
{
  sound.volume(1).play();
}
