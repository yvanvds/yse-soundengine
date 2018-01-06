/*
  ==============================================================================

    synthInterface.cpp
    Created: 6 Jul 2014 10:02:00pm
    Author:  yvan

  ==============================================================================
*/

//#include "synthInterface.hpp"
//#include "synthManager.h"
//#include "../internalHeaders.h"
//#include "../music/note.hpp"
//
//YSE::SYNTH::samplerConfig::samplerConfig()
//: _channel(0) 
//, _rootNote(60)
//, _lowestNote(0)
//, _highestNote(128) 
//, _attackTime(0.f)
//, _releaseTime(0.1f)
//, _maxLength(10.f) {
//
//}
//
//YSE::SYNTH::samplerConfig & YSE::SYNTH::samplerConfig::name(const char * name) {
//  _name = name;
//  return *this;
//}
//
//YSE::SYNTH::samplerConfig & YSE::SYNTH::samplerConfig::file(const char * file) {
//  _file = file;
//  return *this;
//}
//
//YSE::SYNTH::samplerConfig & YSE::SYNTH::samplerConfig::channel(U8 channel) {
//  _channel = channel;
//  return *this;
//}
//
//YSE::SYNTH::samplerConfig & YSE::SYNTH::samplerConfig::root(U8 rootNote) {
//  _rootNote = rootNote;
//  return *this;
//}
//
//YSE::SYNTH::samplerConfig & YSE::SYNTH::samplerConfig::range(U8 low, U8 high) {
//  _lowestNote = low;
//  _highestNote = high;
//  return *this;
//}
//
//YSE::SYNTH::samplerConfig & YSE::SYNTH::samplerConfig::envelope(Flt attack, Flt release, Flt maxLength) {
//  _attackTime = attack;
//  _releaseTime = release;
//  _maxLength = maxLength;
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject::interfaceObject()
//: pimpl(nullptr), numVoices(0)
//{
//}
//
//YSE::SYNTH::interfaceObject::~interfaceObject() {
//  if (pimpl != nullptr) {
//    pimpl->removeInterface();
//    pimpl = nullptr;
//  }
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::create() {
//  assert(pimpl == nullptr);
//  pimpl = SYNTH::Manager().addImplementation(this);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::noteOn(int channel, int noteNumber, float velocity) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = NOTE_ON;
//  // TODO: it's really not good to put these ints in a float vector!!!
//  m.vecValue[0] = (Flt)channel;
//  m.vecValue[1] = (Flt)noteNumber;
//  m.vecValue[2] = velocity;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::noteOn(const MUSIC::note & note) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = NOTE_ON;
//  // TODO: it's really not good to put these ints in a float vector!!!
//  m.vecValue[0] = (Flt)note.getChannel();
//  m.vecValue[1] = note.getPitch();
//  m.vecValue[2] = note.getVolume();
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::noteOff(int channel, int noteNumber) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = NOTE_OFF;
//  // TODO: it's really not good to put these ints in a float vector!!!
//  m.vecValue[0] = (Flt)channel;
//  m.vecValue[1] = (Flt)noteNumber;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::noteOff(const MUSIC::note & note) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = NOTE_OFF;
//  // TODO: it's really not good to put these ints in a float vector!!!
//  m.vecValue[0] = (Flt)note.getChannel();
//  m.vecValue[1] = note.getPitch();
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::allNotesOff(int channel) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = ALL_NOTES_OFF;
//  m.uIntValue[0] = static_cast<U16>(channel);
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::addVoices(const YSE::SYNTH::samplerConfig & voice, int numVoices) {
//  assert(pimpl != nullptr);
//
//  pimpl->addVoices(voice, numVoices);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::addVoices(dspVoice * voice, int numVoices, int channel, int lowestNote, int highestNote) {
//  assert(pimpl != nullptr);
//
//  pimpl->addVoices(voice, numVoices, channel, lowestNote, highestNote);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::removeSound(const std::string & name) {
//  assert(pimpl != nullptr);
//  // removing sound is probably not thread safe
//  // so we ask the implementation for the sound ID
//  // and pass that as a message for thread safe deletion afterwards
//  // messageObject m;
//  // TODO
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::pitchWheel(int channel, int value) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = PITCH_WHEEL;
//  m.intValue[0] = channel;
//  m.intValue[1] = value;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::controller(int channel, int number, int value) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = CONTROLLER;
//  m.intValue[0] = channel;
//  m.intValue[1] = number;
//  m.intValue[2] = value;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::aftertouch(int channel, int noteNumber, int value) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = AFTERTOUCH;
//  m.intValue[0] = channel;
//  m.intValue[1] = noteNumber;
//  m.intValue[2] = value;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::sustain(int channel, bool value) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = SUSTAIN;
//  m.cb.channel = channel;
//  m.cb.value = value;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::sostenuto(int channel, bool value) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = SOSTENUTO;
//  m.cb.channel = channel;
//  m.cb.value = value;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::softPedal(int channel, bool value) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = SOFTPEDAL;
//  m.cb.channel = channel;
//  m.cb.value = value;
//  pimpl->sendMessage(m);
//  return *this;
//}
//
//YSE::SYNTH::interfaceObject & YSE::SYNTH::interfaceObject::onNoteEvent(void(*func)(bool noteOn, float * noteNumber, float * velocity)) {
//  assert(pimpl != nullptr);
//  messageObject m;
//  m.ID = CALLBACK;
//  m.ptr = (void(*))func;
//  pimpl->sendMessage(m);
//  return *this;
//}