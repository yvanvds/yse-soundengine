/*
  ==============================================================================

    midifile.cpp
    Created: 12 Jul 2014 6:55:28pm
    Author:  yvan

  ==============================================================================
*/

#include "midifile.hpp"
#include "midifileImplementation.h"
#include "midifileManager.h"

YSE::MIDI::file::file() : pimpl(nullptr) {
  pimpl = Manager().addImplementation(this);
}

YSE::MIDI::file::~file() {
  if (pimpl != nullptr) {
    pimpl->removeInterface();
  }
}

bool YSE::MIDI::file::create(const std::string & filename) {
  return pimpl->create(filename);
}

void YSE::MIDI::file::play() {
  pimpl->play();
}

void YSE::MIDI::file::pause() {
  pimpl->pause();
}

void YSE::MIDI::file::stop() {
  pimpl->stop();
}
/*
void YSE::MIDI::file::connect(synth * player) {
  pimpl->connect(player);
}

void YSE::MIDI::file::disconnect(synth * player) {
  pimpl->disconnect(player);
}*/