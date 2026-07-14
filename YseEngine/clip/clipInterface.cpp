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
