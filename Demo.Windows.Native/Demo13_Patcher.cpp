#include "stdafx.h"
#include "Demo13_Patcher.h"

using namespace YSE;

void DemoPatcher::Setup() {
  patcher.create(1);

  sine = patcher.AddObject(OBJ::SINE);
  lfo = patcher.AddObject(OBJ::SINE);
  mtof = patcher.AddObject(OBJ::MIDITOFREQUENCY);
  volume = patcher.AddObject("*");
  pHandle * multiplier = patcher.AddObject(OBJ::MULTIPLIER);

  patcher.Connect(mtof, 0, sine, 0);
  patcher.Connect(sine, 0, multiplier, 0);
  patcher.Connect(lfo, 0, multiplier, 1);
  patcher.Connect(multiplier, 0, volume, 0);
  patcher.Connect(volume, 0, patcher.GetOutputHandle(0), 0);

  note = 60.f;
  sine->SetData(0, note);

  lfoFrequency = 4.f;
  lfo->SetData(0, lfoFrequency);

  volume->SetData(1, 0.f);
}


DemoPatcher::DemoPatcher() 
{
  SetTitle("Patcher Demo");
  AddAction('1', "Note Up", std::bind(&DemoPatcher::FreqUp, this));
  AddAction('2', "Note Down", std::bind(&DemoPatcher::FreqDown, this));
  AddAction('3', "LFO Up", std::bind(&DemoPatcher::LfoUp, this));
  AddAction('4', "LFO Down", std::bind(&DemoPatcher::LfoDown, this));
  AddAction('5', "Sound On", std::bind(&DemoPatcher::SoundOn, this));
  AddAction('6', "Sound Off", std::bind(&DemoPatcher::SoundOff, this));
  Setup();
  
  sound.create(patcher).play();
}

void DemoPatcher::FreqUp() {
  note += 1;
  mtof->SetData(0, note);
}

void DemoPatcher::FreqDown() {
  note -= 1;
  mtof->SetData(0, note);
}

void DemoPatcher::LfoUp() {
  lfoFrequency += 0.1f;
  lfo->SetData(0, lfoFrequency);
}

void DemoPatcher::LfoDown() {
  lfoFrequency -= 0.1f;
  lfo->SetData(0, lfoFrequency);
}

void DemoPatcher::SoundOn() {
  volume->SetData(1, 1.f);
}

void DemoPatcher::SoundOff() {
  volume->SetData(1, 0.f);
}