
#include "pRegistry.h"
#include "pObjectList.hpp"

#include "genericObjects/pDac.h"
#include "genericObjects/pAdc.h"
#include "genericObjects/pLine.h"
#include "genericObjects/gSwitch.h"
#include "genericObjects/gGate.h"
#include "genericObjects/gRoute.h"
#include "genericObjects/gIf.h"
#include "genericObjects/gRegexp.h"
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
#include "math/gDrunk.h"
#include "math/gUrn.h"
#include "math/gDecide.h"
#include "math/gProb.h"
#include "math/gAnal.h"
#include "math/gHisto.h"
#include "math/gMean.h"
#include "math/gAccum.h"
#include "math/gCounter.h"
#include "math/gCompare.h"
#include "math/gExtremum.h"
#include "math/gBitwise.h"
#include "math/gReverse.h"
#include "math/gIntDiv.h"
#include "math/gUnaryMath.h"
#include "math/gPow.h"
#include "math/gRound.h"
#include "math/gTrig.h"
#include "math/gAtan2.h"
#include "math/gDbConvert.h"
#include "math/gPolar.h"
#include "math/gClip.h"
#include "math/gPong.h"
#include "math/gSplit.h"
#include "math/gSlide.h"
#include "math/gExpr.h"
#include "math/gVexpr.h"
#include "math/gLinedrive.h"
#include "math/gScale.h"
#include "math/gZmap.h"

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

  // Conditional message dispatch (issue #451)
  Add(OBJ::G_IF, gIf::Create);

  // Regular-expression matching on symbols (issue #452)
  Add(OBJ::G_REGEXP, gRegexp::Create);

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

  // Bounded random walk (issue #453)
  Add(OBJ::G_DRUNK, gDrunk::Create);

  // Random numbers without repetition (issue #454)
  Add(OBJ::G_URN, gUrn::Create);

  // Random 0 or 1 per bang (issue #455)
  Add(OBJ::G_DECIDE, gDecide::Create);

  // Weighted transition table / first-order Markov chain (issue #456)
  Add(OBJ::G_PROB, gProb::Create);

  // Transition histogram over an input stream — the learning half of the pair
  // above, and the object that feeds it (issue #457)
  Add(OBJ::G_ANAL, gAnal::Create);

  // Histogram of the numbers received — the zeroth-order statistic next to
  // .anal's first-order one (issue #458)
  Add(OBJ::G_HISTO, gHisto::Create);

  // Running average of the numbers received (issue #460)
  Add(OBJ::G_MEAN, gMean::Create);

  // Register with add and multiply — the general-purpose counterpart of
  // .counter (issue #461)
  Add(OBJ::G_ACCUM, gAccum::Create);

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

  // Range mapping (issues #443, #444, #447)
  Add(OBJ::G_SCALE, gScale::Create);
  Add(OBJ::G_ZMAP, gZmap::Create);
  Add(OBJ::G_LINEDRIVE, gLinedrive::Create);

  // Range limiting (issues #445, #446)
  Add(OBJ::G_CLIP, gClip::Create);
  Add(OBJ::G_PONG, gPong::Create);

  // Range routing (issue #448)
  Add(OBJ::G_SPLIT, gSplit::Create);

  // Value smoothing (issue #459)
  Add(OBJ::G_SLIDE, gSlide::Create);

  // Expression evaluation (issues #449, #450)
  Add(OBJ::G_EXPR, gExpr::Create);
  Add(OBJ::G_VEXPR, gVexpr::Create);

  // Trigonometric + hyperbolic functions (issue #441)
  Add(OBJ::G_SIN, gSin::Create);
  Add(OBJ::G_COS, gCos::Create);
  Add(OBJ::G_TAN, gTan::Create);
  Add(OBJ::G_ASIN, gAsin::Create);
  Add(OBJ::G_ACOS, gAcos::Create);
  Add(OBJ::G_ATAN, gAtan::Create);
  Add(OBJ::G_ATAN2, gAtan2::Create);
  Add(OBJ::G_SINH, gSinh::Create);
  Add(OBJ::G_COSH, gCosh::Create);
  Add(OBJ::G_TANH, gTanh::Create);
  Add(OBJ::G_ASINH, gAsinh::Create);
  Add(OBJ::G_ACOSH, gAcosh::Create);
  Add(OBJ::G_ATANH, gAtanh::Create);

  // Comparison + logic operators (issue #437)
  Add(OBJ::G_EQUAL, gEqual::Create);
  Add(OBJ::G_NOTEQUAL, gNotEqual::Create);
  Add(OBJ::G_LESS, gLess::Create);
  Add(OBJ::G_LESSEQUAL, gLessEqual::Create);
  Add(OBJ::G_GREATER, gGreater::Create);
  Add(OBJ::G_GREATEREQUAL, gGreaterEqual::Create);
  Add(OBJ::G_LOGICALAND, gLogicalAnd::Create);
  Add(OBJ::G_LOGICALOR, gLogicalOr::Create);

  // Largest / smallest of the inputs (issue #462)
  Add(OBJ::G_MAXIMUM, gMaximum::Create);
  Add(OBJ::G_MINIMUM, gMinimum::Create);

  // Bitwise operators (issue #438)
  Add(OBJ::G_BITAND, gBitAnd::Create);
  Add(OBJ::G_BITOR, gBitOr::Create);
  Add(OBJ::G_SHIFTLEFT, gShiftLeft::Create);
  Add(OBJ::G_SHIFTRIGHT, gShiftRight::Create);

  Add(OBJ::G_METRO, gMetro::Create);

  Add(OBJ::MIDITOFREQUENCY, pMidiToFrequency::Create);
  Add(OBJ::FREQUENCYTOMIDI, pFrequencyToMidi::Create);

  // Unit conversions (issue #442)
  Add(OBJ::G_ATODB, gAToDb::Create);
  Add(OBJ::G_DBTOA, gDbToA::Create);
  Add(OBJ::G_CARTOPOL, gCarToPol::Create);
  Add(OBJ::G_POLTOCAR, gPolToCar::Create);

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
