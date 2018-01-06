#include "stdafx.h"
#include "Demo10_FilePosition.h"


DemoFilePosition::DemoFilePosition()
{
  SetTitle("Sound File Position");
  AddAction('z', "Zero", std::bind(&DemoFilePosition::Zero, this));
  AddAction('1', "One", std::bind(&DemoFilePosition::One, this));
  AddAction('2', "Two", std::bind(&DemoFilePosition::Two, this));
  AddAction('3', "Three", std::bind(&DemoFilePosition::Three, this));
  AddAction('4', "Four", std::bind(&DemoFilePosition::Four, this));
  AddAction('5', "Five", std::bind(&DemoFilePosition::Five, this));
  AddAction('6', "Six", std::bind(&DemoFilePosition::Six, this));
  AddAction('7', "Seven", std::bind(&DemoFilePosition::Seven, this));
  AddAction('8', "Eight", std::bind(&DemoFilePosition::Eight, this));
  AddAction('9', "Nine", std::bind(&DemoFilePosition::Nine, this));

  sound.create("..\\TestResources\\countdown.ogg", nullptr, true).play();
}


DemoFilePosition::~DemoFilePosition()
{
}

void DemoFilePosition::ExplainDemo()
{ 
  std::cout << "This demo instantly changes the playhead in a sound file." << std::endl;
}

void DemoFilePosition::Zero()
{
  sound.time(11.2f * 44100);
}

void DemoFilePosition::One()
{
  sound.time(10.0f * 44100);
}

void DemoFilePosition::Two()
{
  sound.time(9.0f * 44100);
}

void DemoFilePosition::Three()
{
  sound.time(8.0f * 44100);
}

void DemoFilePosition::Four()
{
  sound.time(6.7f * 44100);
}

void DemoFilePosition::Five()
{
  sound.time(5.5f * 44100);
}

void DemoFilePosition::Six()
{
  sound.time(4.3f * 44100);
}

void DemoFilePosition::Seven()
{
  sound.time(3.2f * 44100);
}

void DemoFilePosition::Eight()
{
  sound.time(2.0f * 44100);
}

void DemoFilePosition::Nine()
{
  sound.time(1.0f * 44100);
}
