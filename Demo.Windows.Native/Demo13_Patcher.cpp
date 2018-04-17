#include "stdafx.h"
#include "Demo13_Patcher.h"
#include <fstream>

using namespace YSE;

void DemoPatcher::Setup() {
  patcher.create(1);

  sine = patcher.CreateObject(OBJ::D_SINE);
  lfo = patcher.CreateObject(OBJ::D_SINE);
  mtof = patcher.CreateObject(OBJ::MIDITOFREQUENCY);
  volume = patcher.CreateObject("~*");
  pHandle * multiplier = patcher.CreateObject(OBJ::D_MULTIPLY);
  pHandle * dac = patcher.CreateObject(OBJ::D_DAC);

  pHandle * line = patcher.CreateObject(OBJ::D_LINE);
  line->SetParams("0 100");

  patcher.Connect(mtof, 0, line, 0);
  patcher.Connect(line, 0, sine, 0);
  patcher.Connect(sine, 0, multiplier, 0);
  patcher.Connect(lfo, 0, multiplier, 1);
  patcher.Connect(multiplier, 0, volume, 0);
  patcher.Connect(volume, 0, dac, 0);

  note = 60.f;
  mtof->SetFloatData(0, note);
  sine->SetParams("440");

  lfoFrequency = 4.f;
  lfo->SetParams("4");

  pHandle * lp = patcher.CreateObject(OBJ::D_LOWPASS);

  volume->SetParams("1");
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
  AddAction('7', "Save to File", std::bind(&DemoPatcher::SaveToFile, this));
  Setup();
  
  sound.create(patcher).play();
}

void DemoPatcher::FreqUp() {
  note += 1;
  mtof->SetFloatData(0, note);
}

void DemoPatcher::FreqDown() {
  note -= 1;
  mtof->SetFloatData(0, note);
}

void DemoPatcher::LfoUp() {
  lfoFrequency += 0.1f;
  lfo->SetFloatData(0, lfoFrequency);
}

void DemoPatcher::LfoDown() {
  lfoFrequency -= 0.1f;
  lfo->SetFloatData(0, lfoFrequency);
}

void DemoPatcher::SoundOn() {
  volume->SetFloatData(1, 1.f);
}

void DemoPatcher::SoundOff() {
  volume->SetFloatData(1, 0.f);
}

void DemoPatcher::SaveToFile() {
  std::string result = patcher.DumpJSON();
  std::ofstream out("patcher.json");
  out << result;
  out.close();
}