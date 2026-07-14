/*
  ==============================================================================

    clipInterface.cpp
    Created for issue #250 — clip transport.

    Public YSE::clip handle. Mirrors MIDI::file: the constructor registers an
    audio-thread impl through the manager, and the destructor orphans it for the
    slow pool to reap. All setters forward to the impl.

  ==============================================================================
*/

#include "clip.hpp"
#include "clipTransport.h"
#include "clipManager.h"
#if YSE_ENABLE_MIDI_DEVICE
#include "../midi/device.hpp" // YSE::midiOut -> RtMidiOut* resolve (issue #350)
#endif

YSE::clip::clip() : pimpl(nullptr) {
  pimpl = CLIP::Manager().addImplementation(this);
}

YSE::clip::~clip() {
  if (pimpl != nullptr) pimpl->removeInterface();
}

bool YSE::clip::create(const std::string& clockName) {
  return pimpl->bind(clockName);
}

YSE::clip& YSE::clip::setEvents(const std::vector<clipEvent>& events) {
  pimpl->setEvents(events);
  return *this;
}

YSE::clip& YSE::clip::loopLength(double beats) {
  pimpl->loopLength(beats);
  return *this;
}

YSE::clip& YSE::clip::connect(YSE::synth& synth) {
  pimpl->connect(&synth);
  return *this;
}

YSE::clip& YSE::clip::disconnect(YSE::synth& synth) {
  pimpl->disconnect(&synth);
  return *this;
}

#if YSE_ENABLE_MIDI_DEVICE

YSE::clip& YSE::clip::connect(midiOut& out) {
  // rawPort() is nullptr until midiOut::create() opened a port; the transport
  // ignores a null connect, so an unopened midiOut is a silent no-op.
  pimpl->connectMidiOut(out.rawPort());
  return *this;
}

YSE::clip& YSE::clip::disconnect(midiOut& out) {
  pimpl->disconnectMidiOut(out.rawPort());
  return *this;
}

#endif // YSE_ENABLE_MIDI_DEVICE

YSE::clip& YSE::clip::play() {
  pimpl->play();
  return *this;
}

YSE::clip& YSE::clip::stop() {
  pimpl->stop();
  return *this;
}

bool YSE::clip::isPlaying() const {
  return pimpl->isPlaying();
}
