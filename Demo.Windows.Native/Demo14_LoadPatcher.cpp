#include "stdafx.h"
#include "Demo14_LoadPatcher.h"
#include <fstream>

using namespace YSE;

DemoLoadPatcher::DemoLoadPatcher() {
  SetTitle("Patcher Load Demo");
	AddAction('1', "Note Up", std::bind(&DemoLoadPatcher::FreqUp, this));
	AddAction('2', "Note Down", std::bind(&DemoLoadPatcher::FreqDown, this));
	AddAction('3', "LFO Up", std::bind(&DemoLoadPatcher::LfoUp, this));
	AddAction('4', "LFO Down", std::bind(&DemoLoadPatcher::LfoDown, this));
	AddAction('5', "Volume Up", std::bind(&DemoLoadPatcher::VolumeUp, this));
	AddAction('6', "Volume Down", std::bind(&DemoLoadPatcher::VolumeDown, this));
  AddAction('7', "Load a json file", std::bind(&DemoLoadPatcher::LoadPatch1, this));

  patcher.create(1);
	sound.create(patcher);
	sound.play();
}

void DemoLoadPatcher::LoadPatch1() {
  std::ifstream in("..\\TestResources\\patcher.yap");

  if (in.fail()) {
    in.close();
    std::cout << "File not found" << std::endl;
    return;
  }

  std::string result;
  
  in.seekg(0, std::ios::end);
  result.reserve(in.tellg());
  in.seekg(0, std::ios::beg);

  result.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  patcher.ParseJSON(result);
  in.close();

	noteValue = 60.f;
	lfoValue = 4.f;
	volumeValue = 0.5;

	patcher.PassData(noteValue, "pitch");
	patcher.PassData(lfoValue, "lfo");
	patcher.PassData(volumeValue, "volume");
}


void DemoLoadPatcher::FreqUp() {
	patcher.PassData(++noteValue, "pitch");
}

void DemoLoadPatcher::FreqDown() {
	patcher.PassData(--noteValue, "pitch");
}

void DemoLoadPatcher::LfoUp() {
	lfoValue += 0.1f;
	patcher.PassData(lfoValue, "lfo");
}

void DemoLoadPatcher::LfoDown() {
	lfoValue -= 0.1f;
	patcher.PassData(lfoValue, "lfo");
}

void DemoLoadPatcher::VolumeUp() {
	volumeValue += 0.1;
	patcher.PassData(volumeValue, "volume");
}

void DemoLoadPatcher::VolumeDown() {
	volumeValue -= 0.1;
	patcher.PassData(volumeValue, "volume");
}