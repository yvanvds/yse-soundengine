#include "pRegistry.h"
#include "pObjectList.hpp"

#include "genericObjects/pOutput.h"
#include "generatorObjects/pSine.h"
#include "math/pMultiplier.h"
#include "math/pMidiToFrequency.h"

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
  Add(OBJ::OUT, pOutput::Create);

  Add(OBJ::SINE, pSine::Create);
  Add(OBJ::MULTIPLIER, pMultiplier::Create);
  Add(OBJ::MIDITOFREQUENCY, pMidiToFrequency::Create);

  Add(OBJ::HIGHPASS, pHighpass::Create);
  Add(OBJ::LOWPASS, pLowpass::Create);
}

pObject* pRegistry::Get(const char * objectID) {
  auto it = map.find(objectID);
  if (it != map.end()) return it->second();
  return nullptr;
}

void pRegistry::Add(const char * objectID, pObjectFunc f) {
  map.insert(std::pair<std::string, pObjectFunc>(objectID, f));
}