
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
#include "genericObjects/gArray.h"
#include "genericObjects/gArrayAt.h"
#include "genericObjects/gArrayConcat.h"
#include "genericObjects/gArrayEnds.h"
#include "genericObjects/gArrayFind.h"
#include "genericObjects/gArrayIndexMap.h"
#include "genericObjects/gArrayLength.h"
#include "genericObjects/gArrayPermute.h"
#include "genericObjects/gArrayPosition.h"
#include "genericObjects/gArraySetOps.h"
#include "genericObjects/gArraySlice.h"
#include "genericObjects/gArraySort.h"
#include "genericObjects/gArrayStats.h"
#include "genericObjects/gColl.h"
#include "genericObjects/gDict.h"
#include "genericObjects/gDictCompare.h"
#include "genericObjects/gDictDeserialize.h"
#include "genericObjects/gDictGroup.h"
#include "genericObjects/gDictIter.h"
#include "genericObjects/gDictJoin.h"
#include "genericObjects/gDictPack.h"
#include "genericObjects/gDictPrint.h"
#include "genericObjects/gDictRoute.h"
#include "genericObjects/gDictSerialize.h"
#include "genericObjects/gDictSlice.h"
#include "genericObjects/gDictStrip.h"
#include "genericObjects/gDictUnpack.h"
#include "genericObjects/gPrint.h"
// `.loadbang` / `.loadmess` (issue #547): the pair that lets a saved patch
// describe its own starting state, fired by patcherImplementation's post-publish
// pass rather than by anything the objects themselves do.
#include "genericObjects/gLoadbang.h"
#include "genericObjects/gLoadmess.h"
// Subpatchers (issue #545): the façade and the two boundary objects. Storage
// stays flat and only the addressing nests — see gSubpatcher.h. The audio-rate
// boundary pair (issue #764) is the same pass-through carrying a buffer.
#include "genericObjects/gSubpatcher.h"
#include "genericObjects/gInlet.h"
#include "genericObjects/gOutlet.h"
#include "genericObjects/dInlet.h"
#include "genericObjects/dOutlet.h"
#include "genericObjects/gFunbuff.h"
#include "genericObjects/gTable.h"
#include "genericObjects/gMtr.h"
#include "genericObjects/gQlist.h"
#include "genericObjects/gSeq.h"
#include "genericObjects/gTextfile.h"
#include "genericObjects/gCase.h"
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
#include "genericObjects/gZl.h"
#include "genericObjects/gPack.h"
#include "genericObjects/gUnpack.h"
#include "genericObjects/gJoin.h"
#include "genericObjects/gUnjoin.h"
#include "genericObjects/gIter.h"
#include "genericObjects/gListFunnel.h"
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
#include "guiObjects/gRSlider.h"
#include "guiObjects/gXYSlider.h"
#include "guiObjects/gMultiSlider.h"
#include "guiObjects/gMatrixCtrl.h"
#include "guiObjects/gFunction.h"
#include "guiObjects/gKSlider.h"
#include "guiObjects/gNSlider.h"
#include "guiObjects/gNodes.h"
#include "guiObjects/gItemList.h"
#include "guiObjects/gLabelSwitch.h"
#include "guiObjects/gDial.h"
#include "guiObjects/gIncDec.h"
#include "guiObjects/gButton.h"
#include "guiObjects/gToggle.h"
#include "guiObjects/gMessage.h"
#include "guiObjects/gList.h"
#include "guiObjects/gText.h"
#include "guiObjects/gTextEdit.h"
#include "guiObjects/gPreset.h"

#include "time/gClocker.h"
#include "time/gDelay.h"
#include "time/gLine.h"
#include "time/gMetro.h"
#include "time/gPipe.h"
#include "time/gRateLimit.h"
#include "time/gTempo.h"
#include "time/gThresh.h"
#include "time/gTimepoint.h"
#include "time/gTimer.h"
#include "time/gSetClock.h"
#include "time/gTransport.h"
#include "time/gTranslate.h"
#include "time/gWhen.h"

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

// The MIDI sender family. None of these opens a device — they format bytes
// onto a list outlet — so they are compiled and registered on every platform.
// They used to sit behind a bare `#if YSE_WINDOWS`, which made `.noteon` exist
// on Windows and nowhere else for no reason; issue #746 lifted it off the
// whole family at once.
#include "midi/mMidiBendOut.h"
#include "midi/mMidiChannelPressure.h"
#include "midi/mMidiControl.h"
#include "midi/mMidiNoteOff.h"
#include "midi/mMidiNoteOn.h"
#include "midi/mMidiPolyPressure.h"
#include "midi/mMidiProgramChange.h"
// The extended-precision senders (issue #533), siblings of the seven above and
// unconditional with them.
#include "midi/mMidiXOut.h"
// The parameter-number senders (issue #534), unconditional for the same reason:
// they format control changes and open no device.
#include "midi/mMidiRpnOut.h"
// The MPE family (issue #535). Unconditional too: two of the three format bytes
// onto a list outlet and the third decodes bytes off an inlet, so none of them
// needs a device backend to be worth having.
#include "midi/mMpe.h"
// The MIDI codec pair (issue #530) is registered unconditionally below too.
#include "midi/mMidiCodec.h"
// `.midiflush` (issue #537), unconditional for the same reason as the codec: it
// reads bytes and writes bytes, and opens no device.
#include "midi/mMidiFlush.h"
// `.makenote` (issue #538), unconditional too: it emits a pitch and a velocity
// on two int outlets and holds no port, so it drives a patcher-built synth on a
// platform with no MIDI hardware exactly as it drives a rack on one that has.
#include "midi/mMakeNote.h"
// `.stripnote` (issue #539), unconditional for the same reason again: it reads
// a pitch and a velocity off two inlets and writes them to two outlets, and
// opens no device.
#include "midi/mStripNote.h"
// `.flush` (issue #540), unconditional for the same reason once more: it
// watches pitch/velocity pairs on ordinary cords and sends them back out, and
// opens no device.
#include "midi/mFlush.h"
// `.sustain` (issue #541), unconditional once more: it holds note-offs back
// while a pedal is down and sends them when it lifts, all of it on ordinary
// cords, and opens no device.
#include "midi/mSustain.h"
// `.poly` (issue #542), unconditional once more: it allocates pitch/velocity
// pairs to a numbered pool of voices on ordinary cords, and opens no device.
#include "midi/mPoly.h"
// `.borax` (issue #543), unconditional once more: it watches pitch/velocity
// pairs on ordinary cords and reports numbers about them, and opens no device.
#include "midi/mBorax.h"
// `.offer` (issue #544), unconditional once more: it stores x,y number pairs
// handed to it on ordinary cords and gives each y back once, and opens no
// device.
#include "midi/mOffer.h"
// The system-exclusive pair (issue #531). One header, two guards: `.sxformat`
// is compiled everywhere and `.sysexin` only where there is an input port to
// open, so the include itself carries no `#if`.
#include "midi/mSysEx.h"
// mMidiOut is the only patcher sender that depends on the RtMidi-backed device
// backend — it holds an output port; the rest just emit MIDI bytes and don't
// need it. Guarded on YSE_ENABLE_MIDI_DEVICE alone since #746, matching the
// input family below.
#if YSE_ENABLE_MIDI_DEVICE
#include "midi/mMidiOut.h"
// The MIDI input family (issue #529) needs the same RtMidi-backed backend:
// YSE_ENABLE_MIDI_DEVICE is exactly the set of platforms with a port to open.
#include "midi/mMidiIn.h"
// The extended-precision input objects (issue #533) are built on that family's
// plumbing and share its guard exactly.
#include "midi/mMidiXIn.h"
// The parameter-number input objects (issue #534) are built on that same
// plumbing and share its guard exactly.
#include "midi/mMidiRpnIn.h"
// `.midiinfo` (issue #536) enumerates the backend's ports, so it needs the
// backend to exist; same guard again, and nothing else riding along with it
// (issue #754).
#include "midi/mMidiInfo.h"
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

  // Change the case of a message's text — the one operation the symbol family
  // was missing, and the only way to compare a name case-insensitively, since
  // .route, .sel, .match and every named bus compare character for character
  // (issue #810)
  Add(OBJ::G_TOLOWER, gToLower::Create);
  Add(OBJ::G_TOUPPER, gToUpper::Create);

  // The list-processing workhorse: one object whose behaviour a mode word
  // chooses, over the bounded pre-allocated list the whole list family shares
  // (issue #523)
  Add(OBJ::G_ZL, gZl::Create);

  // Build a list out of values arriving at separate inlets — the way into every
  // list-consuming object, over the same bounded list (issue #517)
  Add(OBJ::G_PACK, gPack::Create);

  // The same object with every inlet hot, for the patches where a fresh list
  // should go out the moment any one of its elements changes (issue #518)
  Add(OBJ::G_PAK, gPak::Create);

  // The way back: one list inlet into as many typed outlets as the arguments
  // declare, and the only way a patch gets at the individual elements of a list
  // that arrives from somewhere else (issue #519)
  Add(OBJ::G_UNPACK, gUnpack::Create);

  // The variable-length counterpart of .pack: each inlet holds a whole message
  // rather than one typed atom, and the output is those pieces laid end to end
  // (issue #520)
  Add(OBJ::G_JOIN, gJoin::Create);

  // And its inverse: a list cut into equal groups, one per outlet, with
  // everything left over out the rightmost one, so nothing is dropped for want
  // of an outlet (issue #520)
  Add(OBJ::G_UNJOIN, gUnjoin::Create);

  // Send a list's items out one at a time down a single cord — the family's
  // serialiser, and with .uzi the patcher's second iteration primitive
  // (issue #521)
  Add(OBJ::G_ITER, gIter::Create);

  // The same walk with the index left on: every element leaves as an
  // <index> <element> pair, so .funnel's tag comes from the position in the
  // message rather than from the wiring (issue #522)
  Add(OBJ::G_LISTFUNNEL, gListFunnel::Create);

  // A collection of messages held at addresses — the patcher's first store of
  // more than one thing, and the object presets, note tables, mapping curves
  // and sequences are all written with (issue #494)
  Add(OBJ::G_COLL, gColl::Create);

  // A nested key/value dictionary shared by name — structured data for the
  // patcher and the live-coding DSL, and the value model the dict.* family is
  // written against: addressed by name, resolved on the control thread, never
  // passed down a cord (issue #550)
  Add(OBJ::G_DICT, gDict::Create);

  // The first dict.* operation written against that model: compares the two
  // dictionaries bound by its creation arguments and reports whether they hold
  // the same entries — "did anything change?" without diffing key by key
  // (issue #770)
  Add(OBJ::G_DICT_COMPARE, gDictCompare::Create);

  // Builds a dictionary from serialised text — one JSON list message, exactly
  // what .dict.serialize emits, replacing the bound dictionary whole; the
  // read half of the interchange pair, parsed on the background pool and
  // installed by the block poll (issue #771)
  Add(OBJ::G_DICT_DESERIALIZE, gDictDeserialize::Create);

  // Groups the source dictionary's entries by a value into the target —
  // "<groupValue>::<originalPath>", the dictionary of dictionaries the flat
  // store expresses as a path prefix; both bound by creation argument
  // (issue #772)
  Add(OBJ::G_DICT_GROUP, gDictGroup::Create);

  // Streams the bound dictionary's entries one "<path> <value...>" list at a
  // time, then a done bang — the .uzi/.iter shape applied to structured
  // data; the walk is a snapshot taken at the trigger (issue #773)
  Add(OBJ::G_DICT_ITER, gDictIter::Create);

  // Merges two dictionaries into one — the left overlaid by the right, the
  // right overwriting on a colliding key path, the result replacing the
  // bound target whole (issue #774)
  Add(OBJ::G_DICT_JOIN, gDictJoin::Create);

  // Builds a dictionary from a list of named inlets — the dictionary
  // counterpart of .pack, with its hot/cold rule: the bound dictionary is
  // replaced whole with one entry per key-path argument and its reference
  // sent on, the unit the rest of the dict.* family consumes (issue #775)
  Add(OBJ::G_DICT_PACK, gDictPack::Create);

  // Prints the bound dictionary to the engine log as a nested multi-line
  // JSON document, through .print's lock-free on-ramp — the debugging
  // instrument for structured data, which no sink can otherwise see
  // (issue #776)
  Add(OBJ::G_DICT_PRINT, gDictPrint::Create);

  // Routes the bound dictionary by the keys it holds: the reference — never
  // the contents — leaves the outlet of the leftmost key argument present,
  // or the rightmost reject, so structured messages dispatch to the part of
  // the graph that understands them (issue #777)
  Add(OBJ::G_DICT_ROUTE, gDictRoute::Create);

  // Serialises the bound dictionary to one single-line compact JSON list
  // message — the write half of the interchange pair with .dict.deserialize,
  // and the same document DictToJson builds for a saved patch (issue #778)
  Add(OBJ::G_DICT_SERIALIZE, gDictSerialize::Create);

  // Splits the bound dictionary at a key path: entries under it replace the
  // slice target with the prefix stripped, everything else replaces the
  // remainder target unchanged — the sub-tree extraction the flat store
  // makes an explicit, bounded prefix scan (issue #779)
  Add(OBJ::G_DICT_SLICE, gDictSlice::Create);

  // Removes the bound dictionary's entries under a key path, in place: the
  // branch removal .dict's delete cannot spell, a bounded back-to-front
  // erase scan of every key beginning "<path>::" (issue #780)
  Add(OBJ::G_DICT_STRIP, gDictStrip::Create);

  // Outputs the bound dictionary's values on separate outlets — one outlet
  // per key-path argument, .dict.pack's inverse: a snapshot of the
  // dictionary leaves right to left through SendAtom, a missing path
  // sending nothing rather than a zero (issue #781)
  Add(OBJ::G_DICT_UNPACK, gDictUnpack::Create);

  // An ordered, index-addressed sequence shared by name — the collection type a
  // generative patch actually reaches for, and the value model the array.*
  // family is written against: addressed by name, resolved on the control
  // thread, one atom per element (issue #548)
  Add(OBJ::G_ARRAY, gArray::Create);

  // The first of the array.* operations written against that model: the array
  // is bound from the creation argument and an index arriving on a cord
  // fetches the element at that position — the read .array's own "get" cannot
  // give a running patch (issue #782)
  Add(OBJ::G_ARRAY_AT, gArrayAt::Create);

  // The bound array's length, asked for with a bang and answered as one int —
  // the number every .uzi-driven walk over an array needs before it can start
  // (issue #783)
  Add(OBJ::G_ARRAY_LENGTH, gArrayLength::Create);

  // The end-mutators: the stack and queue operations over the bound array —
  // push/unshift add at an end and emit the reference so the family chains,
  // pop/shift remove from an end and emit the departing element, banging a
  // second outlet when the array is empty (issue #784)
  Add(OBJ::G_ARRAY_PUSH, gArrayPush::Create);
  Add(OBJ::G_ARRAY_POP, gArrayPop::Create);
  Add(OBJ::G_ARRAY_SHIFT, gArrayShift::Create);
  Add(OBJ::G_ARRAY_UNSHIFT, gArrayUnshift::Create);

  // The position-mutators: an insert and its exact inverse at a position that
  // arrives on a cord — insert adds whole-or-nothing at the stored position
  // and emits the reference so the family chains, remove drops the element
  // there and emits it, banging a miss outlet for a position the array does
  // not have (issue #785)
  Add(OBJ::G_ARRAY_INSERT, gArrayInsert::Create);
  Add(OBJ::G_ARRAY_REMOVE, gArrayRemove::Create);

  // The search objects: both are one ArrayFind under one hold of the store's
  // guard — the first position spelling the searched value — and they differ
  // only in reporting. indexof answers in-band, the position or -1 on a miss;
  // index answers on the family's split, the position out one outlet or a
  // miss bang out the other (issue #786)
  Add(OBJ::G_ARRAY_INDEXOF, gArrayIndexOf::Create);
  Add(OBJ::G_ARRAY_INDEX, gArrayIndex::Create);

  // The reordering primitive: a stored map of zero-based indices, applied to
  // the bound array under one hold of the store's guard through a scratch
  // table the object owns — entries may repeat, an index naming no element
  // contributes nothing, and a reorder that lands emits the reference so the
  // family chains (issue #787)
  Add(OBJ::G_ARRAY_INDEXMAP, gArrayIndexMap::Create);

  // The four permutations: each an index order plus the shared apply through
  // a scratch table, under one hold of the store's guard — reverse counts
  // down, rotate wraps a signed amount modulo the length, and scramble /
  // shuffle are one seedable Fisher-Yates body under Max's two names for it,
  // publishing the applied order so .array.indexmap can put a parallel array
  // into the same new order (issue #788)
  Add(OBJ::G_ARRAY_REVERSE, gArrayReverse::Create);
  Add(OBJ::G_ARRAY_ROTATE, gArrayRotate::Create);
  Add(OBJ::G_ARRAY_SCRAMBLE, gArrayScramble::Create);
  Add(OBJ::G_ARRAY_SHUFFLE, gArrayShuffle::Create);

  // The fifth permutation — .zl sort's ordering over the store's elements,
  // stable and bounded, under one hold of the store's guard: numbers before
  // symbols in both directions, numbers by value, symbols by their
  // characters, negative direction descending. Publishes the applied
  // zero-based order before the reference so .array.indexmap can put a
  // parallel array into the same new order (issue #789)
  Add(OBJ::G_ARRAY_SORT, gArraySort::Create);

  // The six statistics: read-only reducers, each one read of the store under
  // one hold of its guard, answered as a scalar after the release. Five
  // reduce the numeric elements (a symbol is skipped) and mode counts every
  // element by its spelling; min/max/mode answer with the element itself,
  // mean/median/stddev with one float, and an empty population bangs the
  // empty outlet. median and mode sort a scratch the object owns, never the
  // shared store (issue #790)
  Add(OBJ::G_ARRAY_MIN, gArrayMin::Create);
  Add(OBJ::G_ARRAY_MAX, gArrayMax::Create);
  Add(OBJ::G_ARRAY_MEAN, gArrayMean::Create);
  Add(OBJ::G_ARRAY_MEDIAN, gArrayMedian::Create);
  Add(OBJ::G_ARRAY_MODE, gArrayMode::Create);
  Add(OBJ::G_ARRAY_STDDEV, gArrayStdDev::Create);

  // The range readers: each outputs a piece of the array as the list text it
  // spells — never a new named array — collected under one hold of the
  // store's guard, with an empty selection on the empty outlet. slice is
  // JS's exclusive end, subarray the inclusive end with the reversed piece
  // permitted, sub its second Max name over one implementation, and split
  // cuts head from tail at a boundary, tail sent first (issue #791)
  Add(OBJ::G_ARRAY_SLICE, gArraySlice::Create);
  Add(OBJ::G_ARRAY_SUBARRAY, gArraySubarray::Create);
  Add(OBJ::G_ARRAY_SUB, gArraySub::Create);
  Add(OBJ::G_ARRAY_SPLIT, gArraySplit::Create);

  // The set operations: read-only, .zl's semantics — a set operation
  // produces a set, each element once at its first occurrence, equality by
  // the spelling. union and sect bind two names at creation and never hold
  // two guards at once (the left array is snapshotted under its guard, the
  // result built against the right under that guard alone — gDictCompare's
  // arrangement); unique thins one array, .zl thin's selection under Max's
  // array.unique name. The result leaves as list text, never as a new named
  // array, and an empty result bangs the empty outlet (issue #792)
  Add(OBJ::G_ARRAY_UNION, gArrayUnion::Create);
  Add(OBJ::G_ARRAY_SECT, gArraySect::Create);
  Add(OBJ::G_ARRAY_UNIQUE, gArrayUnique::Create);

  // The "put these together" pair, both read-only. concat is the left
  // array's elements followed by the right's — everything kept, repeats
  // included, where the set operations thin — on the same two-name binding
  // and snapshot, leaving as list text. join glues one array's elements
  // into one token, the separator between each pair, sent typed and
  // bounded by what a cord carries rather than by the store's element
  // rule — the result is a message, not an element (issue #793)
  Add(OBJ::G_ARRAY_CONCAT, gArrayConcat::Create);
  Add(OBJ::G_ARRAY_JOIN, gArrayJoin::Create);

  // An unordered collection of numbers a patch adds to and removes from — the
  // multiset .coll's addressed store is not, and the object that answers "which
  // notes are held right now" (issue #495)
  Add(OBJ::G_BAG, gBag::Create);

  // A rolling record of everything that went past — the patcher's debugging
  // instrument, and the store nothing decides the contents of (issue #496)
  Add(OBJ::G_CAPTURE, gCapture::Create);

  // The other half of that instrument: say what is passing *now*, one line at a
  // time, into the engine log — the only way to see inside a running graph
  // without inferring it from the audio coming out (issue #546)
  Add(OBJ::G_PRINT, gPrint::Create);

  // The patch's own beginning: a bang, and a message, sent once the parsed graph
  // has been built and published — the only moment at which "loading finished"
  // is true, and the thing that turns a saved graph into a self-contained patch
  // rather than a graph plus a list of things the host must remember to do to it
  // (issue #547)
  Add(OBJ::G_LOADBANG, gLoadbang::Create);
  Add(OBJ::G_LOADMESS, gLoadmess::Create);

  // Encapsulation (issue #545). `patcher` is the façade the parent addresses as
  // one object; `.inlet` / `.outlet` are its boundary. Storage is flat — every
  // object in a patch lives in one object set and is compiled into one
  // GraphState whatever its nesting depth — so none of the three is visible to
  // the audio thread at all.
  //
  // `~inlet` / `~outlet` are the audio-rate half of that boundary (issue #764):
  // the same pass-through carrying a buffer, sharing the one index space with
  // the control-rate pair because a subpatcher has one set of pins per side.
  // They are DSP objects and the control-rate ones are not, which is exactly
  // why they are four objects and not two.
  Add(OBJ::PATCHER, gSubpatcher::Create);
  Add(OBJ::G_INLET, gInlet::Create);
  Add(OBJ::G_OUTLET, gOutlet::Create);
  Add(OBJ::D_INLET, dInlet::Create);
  Add(OBJ::D_OUTLET, dOutlet::Create);

  // A sparse function: x,y pairs kept sorted by x, with a floor lookup and
  // linear interpolation between the stored points — the store behind every
  // breakpoint curve, tuning table and step sequence (issue #497)
  Add(OBJ::G_FUNBUFF, gFunbuff::Create);

  // A fixed-size array of numbers addressed by index — the dense store none of
  // the others is, and the one a wavetable, a velocity curve or a weighted
  // random draw is written with (issue #498)
  Add(OBJ::G_TABLE, gTable::Create);

  // A sequence of messages collected as lines of text — the line-oriented store
  // none of the others is, and the shape a note list, a cue sheet or a
  // configuration block actually arrives in (issue #499)
  Add(OBJ::G_TEXTFILE, gTextfile::Create);

  // A cue list — a stored sequence of messages played back in time, and the
  // first store in the family that plays rather than being read (issue #500)
  Add(OBJ::G_QLIST, gQlist::Create);

  // A multi-track recorder for messages — the tape machine next to .qlist's
  // score, and the object automation is built out of (issue #501)
  Add(OBJ::G_MTR, gMtr::Create);

  // A sequencer of raw MIDI bytes — the same tape machine as .mtr with the MIDI
  // wire format on it instead of patcher messages (issue #502)
  Add(OBJ::G_SEQ, gSeq::Create);

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
  Add(OBJ::G_RSLIDER, gRSlider::Create);
  // A two-dimensional control pad — two correlated axes moving as one gesture,
  // the XY pad two independent `.slider`s cannot express (issue #563)
  Add(OBJ::G_XYSLIDER, gXYSlider::Create);
  // A bank of values as one control — the shape a step sequencer's levels, a
  // graphic EQ's bands or a set of voice gains have, and the one the patcher's
  // point-holding controls and its opaque `.l` between them could not express
  // (issue #554)
  Add(OBJ::G_MULTISLIDER, gMultiSlider::Create);
  // A grid of cell states as one control, addressed and emitted as
  // `<column> <row> <value>` — the same three numbers `.matrix` and `.router`
  // take on their control inlet, so the control and the crossbar it drives need
  // nothing between them (issue #559)
  Add(OBJ::G_MATRIXCTRL, gMatrixCtrl::Create);
  // A breakpoint function editor as one control: a bounded, sorted store of
  // (x, y, curve) breakpoints that answers any x with the curved interpolation
  // between its neighbours and bangs the whole envelope out as a ramp list
  // `.line` and `.bline` consume — every hand-drawn envelope, velocity curve
  // and automation shape is authored in one of these (issue #561)
  Add(OBJ::G_FUNCTION, gFunction::Create);
  // A piano keyboard as one control: 128 GUI cells, one per MIDI pitch, each
  // holding that key's velocity — the first patcher control that expresses a
  // *note* rather than a number, and it speaks the pair-of-ints `.noteon`,
  // `.makenote` and `.flush` already take (issue #555)
  Add(OBJ::G_KSLIDER, gKSlider::Create);
  // The same pitch written on a stave: two cells, the note and how to spell it,
  // because a pitch alone cannot say whether 61 is a C sharp or a D flat
  // (issue #555)
  Add(OBJ::G_NSLIDER, gNSlider::Create);
  // A field of circular nodes a cursor is weighed against: per-node distance out
  // one outlet and a normalised weight out the other, so one XY position
  // crossfades a whole bank. The patcher's morph controller, and the first
  // control whose output is a *computation* over its state rather than the state
  // itself — .scale and .zmap map one number to one, .multislider holds N
  // numbers and computes nothing, and building it by hand needs a distance
  // expression per node plus a sum that cannot be written without a feedback
  // loop (issue #562)
  Add(OBJ::G_NODES, gNodes::Create);
  // The indexed selector family: one implementation of "a bounded index over a
  // named item list" under the three names a host draws differently — a pop-up,
  // a column of buttons, a row of tabs. The patcher had every way of expressing
  // a *number* as a control and no way at all of expressing a bounded named
  // choice (issue #556)
  Add(OBJ::G_UMENU, gUMenu::Create);
  Add(OBJ::G_RADIOGROUP, gRadioGroup::Create);
  Add(OBJ::G_TAB, gTab::Create);
  Add(OBJ::G_DIAL, gDial::Create);
  Add(OBJ::G_INCDEC, gIncDec::Create);
  Add(OBJ::G_BUTTON, gButton::Create);
  Add(OBJ::G_TOGGLE, gToggle::Create);
  // The labelled switch family: one implementation of "an on/off that carries a
  // name" under the two names a host draws differently — a lamp and a pressable
  // labelled rectangle. `.b` and `.t` above hold the same value and cannot say
  // what they are, which leaves a host rendering a headless patch with a grid of
  // anonymous squares (issue #557)
  Add(OBJ::G_LED, gLed::Create);
  Add(OBJ::G_TEXTBUTTON, gTextButton::Create);
  Add(OBJ::G_MESSAGE, gMessage::Create);
  Add(OBJ::G_LIST, gList::Create);
  Add(OBJ::G_TEXT, gText::Create);
  // The one GUI value that is genuinely a mutable string: `.text` above is a
  // fixed label and every other control's string is an immutable creation
  // argument, so this is where a patch receives text from outside (issue #560)
  Add(OBJ::G_TEXTEDIT, gTextEdit::Create);
  // Snapshot and recall of the patch's control values: numbered slots each
  // holding the captured GUI value of every settable control, restored through
  // the ordinary control-thread message path — the feature every instrument
  // needs, and the write half the GUI value protocol (#551) was settled for
  // (issue #564)
  Add(OBJ::G_PRESET, gPreset::Create);

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

  // Delay a bang — the patcher's most basic scheduling primitive, and the first
  // object that can defer anything at all (issue #503)
  Add(OBJ::G_DELAY, gDelay::Create);

  // Delay numbers, lists and symbols — .delay for data, and the first timing
  // object that holds many pending values at once rather than one (issue #504)
  Add(OBJ::G_PIPE, gPipe::Create);

  // Limit the rate of message throughput — two policies for a control stream
  // that arrives faster than anything downstream needs: .speedlim drops what
  // comes too soon, .qlim holds the newest and sends it when the window opens
  // (issue #508)
  Add(OBJ::G_SPEEDLIM, gSpeedlim::Create);
  Add(OBJ::G_QLIM, gQlim::Create);

  // Group the values that arrive close together into one list — the two answers
  // to "when is a group over": .thresh closes it on a gap in the input,
  // .quickthresh on a fixed window from the first value, which is what chord
  // detection needs (issue #509)
  Add(OBJ::G_THRESH, gThresh::Create);
  Add(OBJ::G_QUICKTHRESH, gQuickthresh::Create);

  // Generate a timed ramp of control values toward a target — the control-rate
  // counterpart of ~line, which writes a DSP buffer and so cannot drive
  // anything that is a number rather than a waveform (issue #510)
  Add(OBJ::G_LINE, gLine::Create);

  // The same ramp with the clock taken out: .bline advances one step per bang,
  // so a breakpoint pair counts bangs rather than milliseconds and the patch
  // supplies the timebase (issue #511)
  Add(OBJ::G_BLINE, gBline::Create);

  // Control a named domain clock from inside the patcher — the object that
  // connects a patch to the engine's polytemporal clock system (issue #513)
  Add(OBJ::G_TRANSPORT, gTransport::Create);

  // Create a named domain clock and set its speed — `.transport`'s write half
  // with the play button taken out: a second tempo domain that is turning as
  // soon as the patch loads (issue #515)
  Add(OBJ::G_SETCLOCK, gSetClock::Create);

  // Report where a named domain clock stands, on demand — `.transport`'s read
  // half, without the ownership that would create the clock (issue #514)
  Add(OBJ::G_WHEN, gWhen::Create);

  // Convert a time value between the patcher's two kinds of time — the
  // exchange rate between milliseconds and a named clock's beats (issue #516)
  Add(OBJ::G_TRANSLATE, gTranslate::Create);

  // Bang when a named domain clock reaches a beat position — the patcher's
  // first absolute point on a musical timeline (issue #507)
  Add(OBJ::G_TIMEPOINT, gTimepoint::Create);

  // Count out a musical subdivision of a named domain clock — a metronome that
  // says *where* in the cycle each tick is, not only that one happened (#512)
  Add(OBJ::G_TEMPO, gTempo::Create);

  // Report the elapsed time at a regular interval — a metronome that says how
  // long it has been running, measured rather than tallied (issue #505)
  Add(OBJ::G_CLOCKER, gClocker::Create);

  // Report the elapsed time between two events — the input side of anything
  // rhythm-aware, and the first timing object that consumes time rather than
  // producing it (issue #506)
  Add(OBJ::G_TIMER, gTimer::Create);

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

  // The MIDI sender family. Unconditional since issue #746: these format
  // bytes onto a list outlet and open nothing, so there is no platform they
  // cannot run on, and a patch built on one machine keeps its objects on
  // another.
  Add(OBJ::M_CHANPRESS, mMidiChannelPressure::Create);
  Add(OBJ::M_CONTROL, mMidiControl::Create);
  Add(OBJ::M_NOTEOFF, mMidiNoteOff::Create);
  Add(OBJ::M_NOTEON, mMidiNoteOn::Create);
  Add(OBJ::M_POLYPRESS, mMidiPolyPressure::Create);
  Add(OBJ::M_PROGCHANGE, mMidiProgramChange::Create);
  // Pitch bend (issue #532) — the last channel-voice status the sender family
  // was missing.
  Add(OBJ::M_BENDOUT, mMidiBendOut::Create);

  // The extended-precision senders (issue #533): the same four channel-voice
  // messages the block above already formats, with the bits the 7-bit versions
  // throw away put back — all fourteen of a pitch bend, the MSB/LSB pair of a
  // controller, and the release velocity a note-off has always had room for.
  Add(OBJ::M_XBENDOUT, mXBendOut::Create);
  Add(OBJ::M_XBENDOUT2, mXBendOut2::Create);
  Add(OBJ::M_XCTLOUT, mXCtlOut::Create);
  Add(OBJ::M_XNOTEOUT, mXNoteOut::Create);

  // The parameter-number senders (issue #534). Not a fifth channel-voice
  // message but a sequence of four control changes: two that select a 14-bit
  // parameter number and two that write a 14-bit value into it, which is how
  // MIDI addresses the parameters no controller number reaches.
  Add(OBJ::M_RPNOUT, mRpnOut::Create);
  Add(OBJ::M_NRPNOUT, mNrpnOut::Create);

  // The MPE family (issue #535). MPE is not a new protocol but an agreement
  // about how to use the sixteen channels MIDI already has — one channel per
  // sounding note, so that bend, pressure and controller 74 become per-note —
  // which is why all three of these are ordinary formatters and decoders rather
  // than anything that needs a backend. `.polymidiin`, the fourth object issue
  // #535 names, is not here: in Max it lives inside a `poly~` and receives that
  // object's `mpeevent` routing, and this patcher has no `poly~` for it to live
  // in.
  Add(OBJ::M_MPECONFIG, mMpeConfig::Create);
  Add(OBJ::M_MPEFORMAT, mMpeFormat::Create);
  Add(OBJ::M_MPEPARSE, mMpeParse::Create);

#if YSE_ENABLE_MIDI_DEVICE
  // The one sender that holds a device port, so the one that stays guarded.
  Add(OBJ::M_OUT, mMidiOut::Create);
#endif

  // The MIDI codec pair (issue #530): raw bytes to structure and back. Not
  // guarded on anything, unlike everything else in this block — these two open
  // no device and hold no port, so a patch that decodes a stream from a file,
  // from `.seq` or from a patch works on every platform. See mMidiCodec.h.
  Add(OBJ::M_PARSE, mMidiParse::Create);
  Add(OBJ::M_FORMAT, mMidiFormat::Create);

  // `.midiflush` (issue #537): the safety valve, and unguarded for the same
  // reason the codec pair is. It sits in the stream, passes everything through
  // and remembers what is sounding, so a bang can release exactly the notes a
  // patch stopped mid-phrase left hanging. A patch driving a software synth
  // built out of patcher objects strands notes exactly as one driving hardware
  // does, so this must exist where there is no hardware to blame.
  Add(OBJ::M_MIDIFLUSH, mMidiFlush::Create);

  // `.makenote` (issue #538): the other half of the same problem. `.midiflush`
  // releases notes a patch already stranded; this one makes stranding them
  // impossible, by scheduling the release at the instant of the attack. Every
  // sender downstream of it is a stateless formatter that remembers nothing, so
  // until this existed a patch had to send its own note-offs by hand.
  Add(OBJ::M_MAKENOTE, mMakeNote::Create);

  // `.stripnote` (issue #539): the other end of the same note. `.makenote`
  // guarantees a release is sent; this one guarantees a release is not *acted
  // on*, which is what a patch that only cares about attacks needs. Every note
  // source reports a release as a pitch with velocity 0, so without it a patch
  // triggers twice per key. Unguarded like the two above — it opens no device.
  Add(OBJ::M_STRIPNOTE, mStripNote::Create);

  // `.flush` (issue #540): `.midiflush`'s complement, not its twin. That one
  // watches a byte stream and releases what the *stream* left sounding, so it
  // sits after the formatters; this one watches pitch/velocity pairs and
  // releases what the *patcher* is holding, so it sits before them, where a
  // note has no channel yet. Unguarded like the three above — it opens no
  // device.
  Add(OBJ::M_FLUSH, mFlush::Create);

  // `.sustain` (issue #541): the pedal, for the patch that is not driving the
  // built-in synth. `.flush` remembers the notes that are sounding and releases
  // them on a bang; this one remembers the releases it swallowed while the pedal
  // was down and sends them when it lifts — opposite sets, and they compose in
  // that order. The engine's own synth already defers releases this way
  // internally; this exposes the same rule to patcher logic. Unguarded like the
  // four above — it opens no device.
  Add(OBJ::M_SUSTAIN, mSustain::Create);

  // `.poly` (issue #542): the allocator the four above assume. They keep a
  // patch's notes honest one cord at a time; this one decides *which voice*
  // plays each note and hands its number out with the pair, which is what lets
  // a patch fan one keyboard across N voice chains and still route every
  // note-off back to the chain that is playing it. It follows the engine
  // synth's own allocation and stealing policy rather than inventing a second
  // one. Unguarded like the five above — it opens no device.
  Add(OBJ::M_POLY, mPoly::Create);

  // `.borax` (issue #543): the one that only ever *reads* the note stream the
  // six above write. They keep a patch's notes honest and decide where each one
  // plays; this one answers how many there are, how long they last and how fast
  // they arrive — the input side of adaptive musical behaviour, and questions
  // nothing else in the patcher can be asked. `.timer` measures one interval
  // between two bangs; this measures every note in a polyphonic stream at once.
  // It holds no note anything else does not also hold, which is why it is the
  // one member of the family with no teardown release. Unguarded like the six
  // above — it opens no device.
  Add(OBJ::M_BORAX, mBorax::Create);

  // `.offer` (issue #544): the per-note memory a pitch transformer needs. A
  // note-off carries the pitch the player released, not the one the synth is
  // sounding, so a transposer has to remember the mapping per note and forget it
  // the moment it is used — which is what "one-time number pairs" means and what
  // separates this from `.funbuff`, `.coll` and `.table`, all three of which are
  // read and re-read. Unguarded like the seven above — it opens no device.
  Add(OBJ::M_OFFER, mOffer::Create);

#if YSE_ENABLE_MIDI_DEVICE
  // The MIDI input family (issue #529) — the way *into* a patch. Every object
  // above this line either formats MIDI or sends it; until these existed a
  // patch could not be played from a keyboard, driven by a controller, or
  // sequenced from outside at all.

  // The undecoded byte stream: the whole protocol, for SysEx, song position and
  // anything the decoding objects filter out — and the format `.seq` records.
  Add(OBJ::M_IN, mMidiIn::Create);

  // Notes, and with them a playable patch.
  Add(OBJ::M_NOTEIN, mNoteIn::Create);

  // Knobs, faders, wheels and pedals.
  Add(OBJ::M_CTLIN, mCtlIn::Create);

  // The pitch wheel, at Max's 7-bit resolution; `.xbendin` (#533) has the rest.
  Add(OBJ::M_BENDIN, mBendIn::Create);

  // Program changes, numbered 1-128 as the hardware displays them.
  Add(OBJ::M_PGMIN, mPgmIn::Create);

  // Channel aftertouch — one pressure for the whole channel.
  Add(OBJ::M_TOUCHIN, mTouchIn::Create);

  // Polyphonic key pressure — the per-note counterpart of the above.
  Add(OBJ::M_POLYIN, mPolyIn::Create);

  // System real time: the clock, start, continue and stop a patch follows an
  // external sequencer by.
  Add(OBJ::M_RTIN, mRtIn::Create);

  // The receiving half of the system-exclusive pair (issue #531): a voice dump
  // off a port, with everything that is not a dump filtered out. Guarded with
  // the input family it belongs to; its partner below is not.
  Add(OBJ::M_SYSEXIN, mSysExIn::Create);

  // The extended-precision input objects (issue #533). Same plumbing as the
  // family above — port, subscription, block poll, bounded drain — reading the
  // bytes at the resolution the wire actually carries: a bend as all fourteen
  // of its bits (or as the two that make them), a controller as its MSB/LSB
  // pair, a note-off with its release velocity, and the raw stream framed into
  // whole messages.
  Add(OBJ::M_XBENDIN, mXBendIn::Create);
  Add(OBJ::M_XBENDIN2, mXBendIn2::Create);
  Add(OBJ::M_XCTLIN, mXCtlIn::Create);
  Add(OBJ::M_XNOTEIN, mXNoteIn::Create);
  Add(OBJ::M_XMIDIIN, mXMidiIn::Create);

  // The parameter-number input objects (issue #534). Same plumbing again, with
  // the one piece of real state in the family: a value carries no parameter
  // number of its own, so these two remember per channel what the selecting
  // controllers last pointed at and report only the writes of their own kind.
  Add(OBJ::M_RPNIN, mRpnIn::Create);
  Add(OBJ::M_NRPNIN, mNrpnIn::Create);

  // The port directory (issue #536). Not an input object at all — it opens
  // nothing and receives nothing — but it asks the same backend which ports
  // exist, so it lives and dies with it. It is what lets a patch find out what
  // the bare index every other MIDI object takes actually refers to.
  Add(OBJ::M_MIDIINFO, mMidiInfo::Create);
#endif

  // The building half of the system-exclusive pair (issue #531). Unguarded for
  // the same reason as the codec above: it is arithmetic over bytes and opens
  // no device, so a dump can be built into a file on a platform with no MIDI
  // hardware at all. See mSysEx.h.
  Add(OBJ::M_SXFORMAT, mSxFormat::Create);
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
