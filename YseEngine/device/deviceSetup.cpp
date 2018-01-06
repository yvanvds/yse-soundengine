/*
  ==============================================================================

    deviceImplementation.cpp
    Created: 10 Apr 2014 6:05:33pm
    Author:  yvan

  ==============================================================================
*/

#include "deviceSetup.hpp"
#include "../internalHeaders.h"

YSE::deviceSetup::deviceSetup()
: in(nullptr),
out(nullptr),
sampleRate(0),
bufferSize(0)
{}

YSE::deviceSetup & YSE::deviceSetup::setInput(const device & in) {
  this->in = &in;
  return *this;
}

YSE::deviceSetup & YSE::deviceSetup::setOutput(const device & out) {
  this->out = &out;
  return *this;
}

YSE::deviceSetup & YSE::deviceSetup::setSampleRate(double value) {
  sampleRate = value;
  return *this;
}

YSE::deviceSetup & YSE::deviceSetup::setBufferSize(int value) {
  bufferSize = value;
  return *this;
}

int YSE::deviceSetup::getOutputChannels() const {
  if (out == nullptr) return 0;
  return out->getOutputChannelNames().size();
}



