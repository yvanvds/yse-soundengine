#include "stdafx.h"
#include "Demo14_LoadPatcher.h"
#include <fstream>

using namespace YSE;

DemoLoadPatcher::DemoLoadPatcher() {
  SetTitle("Patcher Load Demo");
  AddAction('1', "Load a json file", std::bind(&DemoLoadPatcher::LoadPatch1, this));

  patcher.create(1);
  sound.create(patcher).play();
}

void DemoLoadPatcher::LoadPatch1() {
  std::ifstream in("01.yap");

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
}