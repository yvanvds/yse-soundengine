
#include "stdafx.h"

#include "Demo13_Patcher.h"
#include <fstream>

using namespace YSE;

// The `// tutorial:*` markers delimit the snippets that
// documentation/source/tutorials/05_patcher.rst includes. Keep them in place.

void DemoPatcher::Setup() {
  // tutorial:create:begin
  patcher.create(1);
  // tutorial:create:end

  // tutorial:objects:begin
  sine = patcher.CreateObject("~sine");
  lfo = patcher.CreateObject(OBJ::D_SINE);
  mtof = patcher.CreateObject(OBJ::MIDITOFREQUENCY);
  volume = patcher.CreateObject("~*");

  controlPitch = patcher.CreateObject(".r", "pitch");
  controlVolume = patcher.CreateObject(".r", "volume");
  controlLFO = patcher.CreateObject(".r", "lfo");

  pHandle* multiplier = patcher.CreateObject(OBJ::D_MULTIPLY);
  pHandle* dac = patcher.CreateObject(OBJ::D_DAC);

  pHandle* line = patcher.CreateObject(OBJ::D_LINE);
  line->SetParams("0 100");
  // tutorial:objects:end

  // tutorial:connect:begin
  patcher.Connect(mtof, 0, line, 0);
  patcher.Connect(line, 0, sine, 0);
  patcher.Connect(sine, 0, multiplier, 0);
  patcher.Connect(lfo, 0, multiplier, 1);
  patcher.Connect(multiplier, 0, volume, 0);
  patcher.Connect(volume, 0, dac, 0);
  // tutorial:connect:end

  // tutorial:receive:begin
  patcher.Connect(controlPitch, 0, mtof, 0);
  patcher.Connect(controlVolume, 0, volume, 1);
  patcher.Connect(controlLFO, 0, lfo, 0);
  // tutorial:receive:end

  // tutorial:loadmess:begin
  // Starting values that travel with the patch: each .loadmess sends its
  // message when a saved copy of this patch is loaded.
  pHandle* initPitch = patcher.CreateObject(OBJ::G_LOADMESS, "60");
  pHandle* initLfo = patcher.CreateObject(OBJ::G_LOADMESS, "4.0");
  pHandle* initVolume = patcher.CreateObject(OBJ::G_LOADMESS, "0.5");
  patcher.Connect(initPitch, 0, mtof, 0);
  patcher.Connect(initLfo, 0, lfo, 0);
  patcher.Connect(initVolume, 0, volume, 1);

  // A patch built in code is never "loaded", so fire them once by hand.
  initPitch->SetBang(0);
  initLfo->SetBang(0);
  initVolume->SetBang(0);
  // tutorial:loadmess:end

  noteValue = 60.f;
  lfoValue = 4.f;
  volumeValue = 0.5;
}

DemoPatcher::DemoPatcher() {
  SetTitle("Patcher Demo");
  AddAction('1', "Note Up", std::bind(&DemoPatcher::FreqUp, this));
  AddAction('2', "Note Down", std::bind(&DemoPatcher::FreqDown, this));
  AddAction('3', "LFO Up", std::bind(&DemoPatcher::LfoUp, this));
  AddAction('4', "LFO Down", std::bind(&DemoPatcher::LfoDown, this));
  AddAction('5', "Volume Up", std::bind(&DemoPatcher::VolumeUp, this));
  AddAction('6', "Volume Down", std::bind(&DemoPatcher::VolumeDown, this));
  AddAction('7', "Save to File", std::bind(&DemoPatcher::SaveToFile, this));
  Setup();

  // tutorial:sound:begin
  sound.create(patcher);
  sound.play();
  // tutorial:sound:end
}

// tutorial:hotkeys:begin
void DemoPatcher::FreqUp() {
  patcher.PassData(++noteValue, "pitch");
}

void DemoPatcher::FreqDown() {
  patcher.PassData(--noteValue, "pitch");
}
// tutorial:hotkeys:end

void DemoPatcher::LfoUp() {
  lfoValue += 0.1f;
  patcher.PassData(lfoValue, "lfo");
}

void DemoPatcher::LfoDown() {
  lfoValue -= 0.1f;
  patcher.PassData(lfoValue, "lfo");
}

void DemoPatcher::VolumeUp() {
  volumeValue += 0.1f;
  patcher.PassData(volumeValue, "volume");
}

void DemoPatcher::VolumeDown() {
  volumeValue -= 0.1f;
  patcher.PassData(volumeValue, "volume");
}

// tutorial:save:begin
void DemoPatcher::SaveToFile() {
  // Written to the working directory, where Demo14 looks first.
  std::ofstream out("patcher.yap");
  out << patcher.DumpJSON();
}
// tutorial:save:end
