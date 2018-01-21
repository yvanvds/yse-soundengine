#include "pRegistry.h"
#include "pObjectList.hpp"

#include "genericObjects\pDac.h"
#include "genericObjects/pLine.h"
#include "generatorObjects/pSine.h"
#include "math/pAdd.h"
#include "math/pSubstract.h"
#include "math/pDivide.h"
#include "math/pMultiplier.h"
#include "math/pMidiToFrequency.h"
#include "math/pFrequencyToMidi.h"

#include "filters\pBandpass.h"
#include "filters\pHighpass.h"
#include "filters\pLowpass.h"

using namespace YSE::PATCHER;

YSE::PATCHER::pRegistry & YSE::PATCHER::Register() {
  static pRegistry s;
  return s;
}

pRegistry::pRegistry() {
  // add all objects here

  // Generic
  Add(OBJ::D_LINE, pLine::Create);

  Add(OBJ::D_SINE, pSine::Create);

  Add(OBJ::D_ADD, pAdd::Create);
  Add(OBJ::D_SUBSTRACT, pSubstract::Create);
  Add(OBJ::D_MULTIPLY, pMultiply::Create);
  Add(OBJ::D_DIVIDE, pDivide::Create);
  
  Add(OBJ::MIDITOFREQUENCY, pMidiToFrequency::Create);
  Add(OBJ::FREQUENCYTOMIDI, pFrequencyToMidi::Create);

  Add(OBJ::D_LOWPASS, pLowpass::Create);
  Add(OBJ::D_BANDPASS, pBandpass::Create);
  Add(OBJ::D_HIGHPASS, pHighpass::Create);
}

pObject* pRegistry::Get(const char * objectID) {
  auto it = map.find(objectID);
  if (it != map.end()) return it->second();
  return nullptr;
}

void pRegistry::Add(const char * objectID, pObjectFunc f) {
  map.insert(std::pair<std::string, pObjectFunc>(objectID, f));
}

bool pRegistry::IsValidObject(const char * objectID) {
  auto it = map.find(objectID);
  return it != map.end();
}