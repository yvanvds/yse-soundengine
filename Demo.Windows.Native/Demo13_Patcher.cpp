
#include "stdafx.h"

#include "Demo13_Patcher.h"
#include <fstream>

using namespace YSE;

void DemoPatcher::Setup() {
  patcher.create(1);

  sine = patcher.CreateObject("~sine");
  lfo = patcher.CreateObject(OBJ::D_SINE);
  mtof = patcher.CreateObject(OBJ::MIDITOFREQUENCY);
  volume = patcher.CreateObject("~*");
  
	controlPitch = patcher.CreateObject(".r", "pitch");
	controlVolume = patcher.CreateObject(".r", "volume");
	controlLFO = patcher.CreateObject(".r", "lfo");
	
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

	patcher.Connect(controlPitch, 0, mtof, 0);
	patcher.Connect(controlVolume, 0, volume, 1);
	patcher.Connect(controlLFO, 0, lfo, 0);

  noteValue = 60.f;
	lfoValue = 4.f;
	volumeValue = 0.5;

	patcher.PassData(noteValue, "pitch");
	patcher.PassData(lfoValue, "lfo");
	patcher.PassData(volumeValue, "volume");
}


DemoPatcher::DemoPatcher() 
{
  SetTitle("Patcher Demo");
  AddAction('1', "Note Up", std::bind(&DemoPatcher::FreqUp, this));
  AddAction('2', "Note Down", std::bind(&DemoPatcher::FreqDown, this));
  AddAction('3', "LFO Up", std::bind(&DemoPatcher::LfoUp, this));
  AddAction('4', "LFO Down", std::bind(&DemoPatcher::LfoDown, this));
  AddAction('5', "Volume Up", std::bind(&DemoPatcher::VolumeUp, this));
  AddAction('6', "Volume Down", std::bind(&DemoPatcher::VolumeDown, this));
  AddAction('7', "Save to File", std::bind(&DemoPatcher::SaveToFile, this));
  Setup();
  
	sound.create(patcher);
	sound.play();
}

void DemoPatcher::FreqUp() {
	patcher.PassData(++noteValue, "pitch");
}

void DemoPatcher::FreqDown() {
	patcher.PassData(--noteValue, "pitch");
}

void DemoPatcher::LfoUp() {
  lfoValue += 0.1f;
	patcher.PassData(lfoValue, "lfo");
}

void DemoPatcher::LfoDown() {
	lfoValue -= 0.1f;
	patcher.PassData(lfoValue, "lfo");
}

void DemoPatcher::VolumeUp() {
	volumeValue += 0.1;
	patcher.PassData(volumeValue, "volume");
}

void DemoPatcher::VolumeDown() {
	volumeValue -= 0.1;
	patcher.PassData(volumeValue, "volume");
}

void DemoPatcher::SaveToFile() {
  std::string result = patcher.DumpJSON();
  std::ofstream out("patcher.yap");
  out << result;
  out.close();
}
