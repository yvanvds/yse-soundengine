
#include "pRegistry.h"
#include "pObjectList.hpp"

#include "genericObjects/pDac.h"
#include "genericObjects/pAdc.h"
#include "genericObjects/pLine.h"
#include "genericObjects/gSwitch.h"
#include "genericObjects/gGate.h"
#include "genericObjects/gMatrix.h"
#include "genericObjects/gRoute.h"
#include "genericObjects/gRoutePass.h"
#include "genericObjects/gRouter.h"
#include "genericObjects/gSel.h"
#include "genericObjects/gTrigger.h"
#include "genericObjects/gBangBang.h"
#include "genericObjects/gOneBang.h"
#include "genericObjects/gNext.h"
#include "genericObjects/gBondo.h"
#include "genericObjects/gBucket.h"
#include "genericObjects/gBuddy.h"
#include "genericObjects/gCycle.h"
#include "genericObjects/gMatch.h"
#include "genericObjects/gAffix.h"
#include "genericObjects/gCharCode.h"
#include "genericObjects/gBag.h"
#include "genericObjects/gCapture.h"
#include "genericObjects/gColl.h"
#include "genericObjects/gFunbuff.h"
#include "genericObjects/gCombine.h"
#include "genericObjects/gSpell.h"
#include "genericObjects/gSprintf.h"
#include "genericObjects/gSubstitute.h"
#include "genericObjects/gSymbol.h"
#include "genericObjects/gDecode.h"
#include "genericObjects/gForward.h"
#include "genericObjects/gValue.h"
#include "genericObjects/gFunnel.h"
#include "genericObjects/gSpray.h"
#include "genericObjects/gUzi.h"
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
#include "math/gRunningExtremum.h"
#include "math/gPast.h"
#include "math/gChange.h"
#include "math/gTogEdge.h"
#include "math/gBitwise.h"
#include "math/gReverse.h"
#include "math/gSwap.h"
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

  // Audio output of the graph. Registered so ~dac is a valid, documented type
  // and appears in the metadata snapshot a UI builds its palette from (issue
  // #624); like ~adc below, the rendered graph builds a channel-matched
  // instance in patcherImplementation::CreateObjectUnlocked rather than via
  // this Create().
  Add(OBJ::D_DAC, pDac::Create);

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

  // Route a complete message by what its first item matches, without consuming
  // that item (issue #483)
  Add(OBJ::G_ROUTEPASS, gRoutePass::Create);

  // Bang the outlet whose selector the input matches (issue #465)
  Add(OBJ::G_SEL, gSel::Create);

  // Send one input to many outlets in right-to-left order (issue #466)
  Add(OBJ::G_TRIGGER, gTrigger::Create);

  // Fan one input out as a bang from many outlets, right to left (issue #467)
  Add(OBJ::G_BANGBANG, gBangBang::Create);

  // Pass one bang per arming, reject the rest (issue #470)
  Add(OBJ::G_ONEBANG, gOneBang::Create);
  Add(OBJ::G_NEXT, gNext::Create);

  // Detect a sequence of values as it arrives (issue #472)
  Add(OBJ::G_MATCH, gMatch::Create);

  // Emit N bangs immediately, with a running index — the patcher's loop
  // (issue #473)
  Add(OBJ::G_UZI, gUzi::Create);

  // Hold one value per inlet and release the whole set together (issue #474)
  Add(OBJ::G_BONDO, gBondo::Create);

  // Wait until every inlet has data, then release once (issue #475)
  Add(OBJ::G_BUDDY, gBuddy::Create);

  // Deal successive messages to successive outlets, wrapping round (issue #477)
  Add(OBJ::G_CYCLE, gCycle::Create);

  // Shift values along a chain of outlets, one stage per input (issue #478)
  Add(OBJ::G_BUCKET, gBucket::Create);

  // Distribute the values of a list to numbered outlets (issue #479)
  Add(OBJ::G_SPRAY, gSpray::Create);

  // Tag incoming data with its inlet number and merge to one outlet (issue #480)
  Add(OBJ::G_FUNNEL, gFunnel::Create);

  // Send 1 out a selected outlet and 0 out every other (issue #481)
  Add(OBJ::G_DECODE, gDecode::Create);

  // A message crossbar: any inlet to any set of outlets, connections set by
  // messages rather than by patch cords (issue #482)
  Add(OBJ::G_ROUTER, gRouter::Create);

  // The same crossbar with a gain per cell rather than a switch: a
  // control-domain patchbay for routing modulation (issue #484)
  Add(OBJ::G_MATRIX, gMatrix::Create);

  // Conditional message dispatch (issue #451)
  Add(OBJ::G_IF, gIf::Create);

  // Regular-expression matching on symbols (issue #452)
  Add(OBJ::G_REGEXP, gRegexp::Create);

  // Message construction: put a stored message in front of, or after, every
  // message that arrives — how a patch builds a list with a leading selector
  // out of a value it computed (issue #487)
  Add(OBJ::G_PREPEND, gPrepend::Create);
  Add(OBJ::G_APPEND, gAppend::Create);

  // Find-and-replace inside a message — the edit .prepend / .append and the
  // routing family cannot make, since they only ever see its ends (issue #488)
  Add(OBJ::G_SUBSTITUTE, gSubstitute::Create);

  // Build a message out of values rather than out of whole tokens — the round
  // trip to the host application .prepend / .append / .substitute still needed,
  // since none of them can spell a number into the middle of a word (issue #489)
  Add(OBJ::G_SPRINTF, gSprintf::Create);

  // Collapse a message into a single token and expand it back — the round trip
  // that lets structured data ride through anything that takes a name, and the
  // one conversion the message-construction family could not express, since a
  // patcher message is text and its readers all split on whitespace (issue #490)
  Add(OBJ::G_TOSYMBOL, gToSymbol::Create);
  Add(OBJ::G_FROMSYMBOL, gFromSymbol::Create);

  // Join items held one per inlet into a single symbol — the stateful,
  // cross-inlet half of what .tosymbol does within a single message, and the
  // only way a patch can assemble a name out of parts that arrive at different
  // times from different sources (issue #491)
  Add(OBJ::G_COMBINE, gCombine::Create);

  // Spell a message out as the character codes of its text — the one direction
  // the message-construction family could not go, since every other object in it
  // takes text apart into smaller text and this one leaves the text model
  // entirely, handing a patch numbers it can do arithmetic on (issue #492)
  Add(OBJ::G_SPELL, gSpell::Create);

  // The character/integer round trip .spell only had one half of: .atoi holds
  // the codes of what it converted so a patch can add to them a piece at a time
  // and send them when it likes, and .itoa is the way back into text, without
  // which codes computed inside a patch could never become a name again
  // (issue #493)
  Add(OBJ::G_ATOI, gAtoi::Create);
  Add(OBJ::G_ITOA, gItoa::Create);

  // A collection of messages held at addresses — the patcher's first store of
  // more than one thing, and the object presets, note tables, mapping curves
  // and sequences are all written with (issue #494)
  Add(OBJ::G_COLL, gColl::Create);

  // An unordered collection of numbers a patch adds to and removes from — the
  // multiset .coll's addressed store is not, and the object that answers "which
  // notes are held right now" (issue #495)
  Add(OBJ::G_BAG, gBag::Create);

  // A rolling record of everything that went past — the patcher's debugging
  // instrument, and the store nothing decides the contents of (issue #496)
  Add(OBJ::G_CAPTURE, gCapture::Create);

  // A sparse function: x,y pairs kept sorted by x, with a floor lookup and
  // linear interpolation between the stored points — the store behind every
  // breakpoint curve, tuning table and step sequence (issue #497)
  Add(OBJ::G_FUNBUFF, gFunbuff::Create);

  Add(OBJ::G_RECEIVE, gReceive::Create);
  Add(OBJ::G_SEND, gSend::Create);

  // A .s whose destination name arrives as a message rather than being fixed at
  // creation (issue #485)
  Add(OBJ::G_FORWARD, gForward::Create);

  // A named cell shared by every .value of that name — the pull half of what
  // .s / .r push (issue #486)
  Add(OBJ::G_VALUE, gValue::Create);

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

  // Reverse the order of a pair of numbers — the general form of .!- / .!/
  // (issue #476)
  Add(OBJ::G_SWAP, gSwap::Create);

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

  // Running extremes — the pair that stores what it emits (issue #463)
  Add(OBJ::G_PEAK, gPeak::Create);
  Add(OBJ::G_TROUGH, gTrough::Create);

  // Bang once when a threshold is crossed (issue #464)
  Add(OBJ::G_PAST, gPast::Create);

  // Pass a number on only when it differs from the last one (issue #468)
  Add(OBJ::G_CHANGE, gChange::Create);

  // Bang on a zero crossing, in either direction (issue #469)
  Add(OBJ::G_TOGEDGE, gTogEdge::Create);

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
