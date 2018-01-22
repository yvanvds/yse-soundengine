#include "stdafx.h"
#include "MenuDsp.h"
#include "Demo07_DspSource.h"
#include "Demo13_Patcher.h"
#include "Demo14_LoadPatcher.h"

MenuDsp::MenuDsp()
{
  SetTitle("Dsp Examples");
  AddAction('1', "Dsp Source", std::bind(&MenuDsp::DspSourceDemo, this));
  AddAction('2', "Patcher", std::bind(&MenuDsp::PatcherDemo, this));
  AddAction('3', "Load Patcher", std::bind(&MenuDsp::LoadPatcherDemo, this));
}

void MenuDsp::DspSourceDemo()
{
  DemoDspSource demo;
  demo.Run();
  ShowMenu();
}

void MenuDsp::PatcherDemo() {
  DemoPatcher demo;
  demo.Run();
  ShowMenu();
}

void MenuDsp::LoadPatcherDemo() {
  DemoLoadPatcher demo;
  demo.Run();
  ShowMenu();
}
