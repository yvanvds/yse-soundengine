#include "stdafx.h"

#include "Demo14_LoadPatcher.h"
#include <fstream>
#include <iterator>

using namespace YSE;

// The `// tutorial:*` markers delimit the snippets that
// documentation/source/tutorials/05_patcher.rst includes. Keep them in place.

DemoLoadPatcher::DemoLoadPatcher() {
  SetTitle("Patcher Load Demo");
  AddAction('1', "Note Up", std::bind(&DemoLoadPatcher::FreqUp, this));
  AddAction('2', "Note Down", std::bind(&DemoLoadPatcher::FreqDown, this));
  AddAction('3', "LFO Up", std::bind(&DemoLoadPatcher::LfoUp, this));
  AddAction('4', "LFO Down", std::bind(&DemoLoadPatcher::LfoDown, this));
  AddAction('5', "Volume Up", std::bind(&DemoLoadPatcher::VolumeUp, this));
  AddAction('6', "Volume Down", std::bind(&DemoLoadPatcher::VolumeDown, this));
  AddAction('7', "Load a json file", std::bind(&DemoLoadPatcher::LoadPatch1, this));

  // tutorial:attach:begin
  patcher.create(1);
  sound.create(patcher);
  sound.play();
  // tutorial:attach:end
}

void DemoLoadPatcher::LoadPatch1() {
  // tutorial:load:begin
  // Prefer the file Demo13 saved in the working directory, and fall back to
  // the copy that ships with the repository.
  std::ifstream in("patcher.yap");
  if (!in) in.open(YSE_TEST_RESOURCES_DIR "/patcher.yap");
  if (!in) {
    std::cout << "File not found" << std::endl;
    return;
  }

  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  patcher.Clear();  // ParseJSON adds to what is there
  patcher.ParseJSON(json);
  // tutorial:load:end

  // The patch's .loadmess objects have already set these; keep the hotkeys'
  // copies in step with them.
  noteValue = 60.f;
  lfoValue = 4.f;
  volumeValue = 0.5;
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
  volumeValue += 0.1f;
  patcher.PassData(volumeValue, "volume");
}

void DemoLoadPatcher::VolumeDown() {
  volumeValue -= 0.1f;
  patcher.PassData(volumeValue, "volume");
}
