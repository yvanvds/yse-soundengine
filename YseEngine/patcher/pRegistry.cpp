
#include "pRegistry.h"
#include "pObjectList.hpp"

#include "genericObjects/pDac.h"
#include "genericObjects/pAdc.h"
#include "genericObjects/pLine.h"
#include "genericObjects/gSwitch.h"
#include "genericObjects/gGate.h"
#include "genericObjects/gRoute.h"
#include "genericObjects/gReceive.h"
#include "genericObjects/gSend.h"

#include "generatorObjects/pSine.h"
#include "generatorObjects/dSaw.h"
#include "generatorObjects/dNoise.h"

#include "guiObjects/gInt.h"
#include "guiObjects/gFloat.h"
#include "guiObjects/gSlider.h"
#include "guiObjects/gButton.h"
#include "guiObjects/gToggle.h"
#include "guiObjects/gMessage.h"
#include "guiObjects/gList.h"
#include "guiObjects/gText.h"

#include "time/gMetro.h"

#include "math/dAdd.h"
#include "math/dClip.h"
#include "math/dSubstract.h"
#include "math/dDivide.h"
#include "math/dMultiply.h"
#include "math/pMidiToFrequency.h"
#include "math/pFrequencyToMidi.h"

#include "filters/pBandpass.h"
#include "filters/pHighpass.h"
#include "filters/pLowpass.h"
#include "filters/dVcf.h"

#include "math/gAdd.h"
#include "math/gDivide.h"
#include "math/gMultiply.h"
#include "math/gSubstract.h"
#include "math/gRandom.h"
#include "math/gCounter.h"
#include "math/gCompare.h"
#include "math/gBitwise.h"
#include "math/gReverse.h"
#include "math/gIntDiv.h"
#include "math/gUnaryMath.h"
#include "math/gPow.h"
#include "math/gRound.h"

#if YSE_WINDOWS
#include "midi/mMidiChannelPressure.h"
#include "midi/mMidiControl.h"
#include "midi/mMidiNoteOff.h"
#include "midi/mMidiNoteOn.h"
#include "midi/mMidiPolyPressure.h"
#include "midi/mMidiProgramChange.h"
#endif
// mMidiOut is the only patcher midi object that depends on the RtMidi-backed
// device backend; the other six just emit MIDI bytes and don't need it.
#if YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE
#include "midi/mMidiOut.h"
#endif

using namespace YSE::PATCHER;

YSE::PATCHER::pRegistry& YSE::PATCHER::Register() {
  static pRegistry s;
  return s;
}

pRegistry::pRegistry() {
  // add all objects here

  // Generic DSP
  Add(OBJ::D_LINE, pLine::Create);

  // Audio input into the graph (patcher-as-insert, issue #167). Registered so
  // ~adc is a valid, documented type and appears in the metadata snapshot; the
  // rendered graph builds a channel-matched instance in
  // patcherImplementation::CreateObjectUnlocked rather than via this Create().
  Add(OBJ::D_ADC, pAdc::Create);

  Add(OBJ::D_SINE, pSine::Create);
  Add(OBJ::D_SAW, dSaw::Create);
  Add(OBJ::D_NOISE, dNoise::Create);

  Add(OBJ::G_SWITCH, gSwitch::Create);
  Add(OBJ::G_GATE, gGate::Create);
  Add(OBJ::G_ROUTE, gRoute::Create);
  Add(OBJ::G_RECEIVE, gReceive::Create);
  Add(OBJ::G_SEND, gSend::Create);

  Add(OBJ::D_ADD, dAdd::Create);
  Add(OBJ::D_SUBSTRACT, dSubstract::Create);
  Add(OBJ::D_MULTIPLY, dMultiply::Create);
  Add(OBJ::D_DIVIDE, dDivide::Create);
  Add(OBJ::D_CLIP, dClip::Create);

  // Generic GUI
  Add(OBJ::G_INT, gInt::Create);
  Add(OBJ::G_FLOAT, gFloat::Create);
  Add(OBJ::G_SLIDER, gSlider::Create);
  Add(OBJ::G_BUTTON, gButton::Create);
  Add(OBJ::G_TOGGLE, gToggle::Create);
  Add(OBJ::G_MESSAGE, gMessage::Create);
  Add(OBJ::G_LIST, gList::Create);
  Add(OBJ::G_TEXT, gText::Create);

  Add(OBJ::G_ADD, gAdd::Create);
  Add(OBJ::G_DIVIDE, gDivide::Create);
  Add(OBJ::G_MULTIPLY, gMultiply::Create);
  Add(OBJ::G_SUBSTRACT, gSubstract::Create);
  Add(OBJ::G_RANDOM, gRandom::Create);
  Add(OBJ::G_COUNTER, gCounter::Create);

  // Remaining arithmetic operators (issue #439)
  Add(OBJ::G_REVERSESUBSTRACT, gReverseSubstract::Create);
  Add(OBJ::G_REVERSEDIVIDE, gReverseDivide::Create);
  Add(OBJ::G_MODULO, gModulo::Create);
  Add(OBJ::G_INTDIVIDE, gIntDivide::Create);

  // Elementary math functions (issue #440)
  Add(OBJ::G_ABS, gAbs::Create);
  Add(OBJ::G_SQRT, gSqrt::Create);
  Add(OBJ::G_POW, gPow::Create);
  Add(OBJ::G_ROUND, gRound::Create);

  // Comparison + logic operators (issue #437)
  Add(OBJ::G_EQUAL, gEqual::Create);
  Add(OBJ::G_NOTEQUAL, gNotEqual::Create);
  Add(OBJ::G_LESS, gLess::Create);
  Add(OBJ::G_LESSEQUAL, gLessEqual::Create);
  Add(OBJ::G_GREATER, gGreater::Create);
  Add(OBJ::G_GREATEREQUAL, gGreaterEqual::Create);
  Add(OBJ::G_LOGICALAND, gLogicalAnd::Create);
  Add(OBJ::G_LOGICALOR, gLogicalOr::Create);

  // Bitwise operators (issue #438)
  Add(OBJ::G_BITAND, gBitAnd::Create);
  Add(OBJ::G_BITOR, gBitOr::Create);
  Add(OBJ::G_SHIFTLEFT, gShiftLeft::Create);
  Add(OBJ::G_SHIFTRIGHT, gShiftRight::Create);

  Add(OBJ::G_METRO, gMetro::Create);

  Add(OBJ::MIDITOFREQUENCY, pMidiToFrequency::Create);
  Add(OBJ::FREQUENCYTOMIDI, pFrequencyToMidi::Create);

  Add(OBJ::D_LOWPASS, pLowpass::Create);
  Add(OBJ::D_BANDPASS, pBandpass::Create);
  Add(OBJ::D_HIGHPASS, pHighpass::Create);
  Add(OBJ::D_VCF, dVcf::Create);

#if YSE_WINDOWS
  Add(OBJ::M_CHANPRESS, mMidiChannelPressure::Create);
  Add(OBJ::M_CONTROL, mMidiControl::Create);
  Add(OBJ::M_NOTEOFF, mMidiNoteOff::Create);
  Add(OBJ::M_NOTEON, mMidiNoteOn::Create);
  Add(OBJ::M_POLYPRESS, mMidiPolyPressure::Create);
  Add(OBJ::M_PROGCHANGE, mMidiProgramChange::Create);
#endif
#if YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE
  Add(OBJ::M_OUT, mMidiOut::Create);
#endif
}

pObject* pRegistry::Get(const std::string& objectID) {
  auto it = map.find(objectID);
  if (it != map.end()) return it->second();
  return nullptr;
}

void pRegistry::Add(const std::string& objectID, pObjectFunc f) {
  map.insert(std::pair<std::string, pObjectFunc>(objectID, f));
}

bool pRegistry::IsValidObject(const char* objectID) {
  auto it = map.find(objectID);
  return it != map.end();
}

std::vector<std::string> pRegistry::AllNames() const {
  std::vector<std::string> names;
  names.reserve(map.size());
  for (const auto& entry : map) {
    names.push_back(entry.first);
  }
  return names;
}
