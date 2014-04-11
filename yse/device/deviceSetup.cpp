/*
  ==============================================================================

    deviceImplementation.cpp
    Created: 10 Apr 2014 6:05:33pm
    Author:  yvan

  ==============================================================================
*/

#include "deviceSetup.hpp"
#include "../internalHeaders.h"

YSE::DEVICE::setupObject::setupObject()
: in(nullptr),
out(nullptr),
sampleRate(0),
bufferSize(0)
{}

YSE::DEVICE::setupObject & YSE::DEVICE::setupObject::setInput(const interfaceObject & in) {
  this->in = &in;
  return *this;
}

YSE::DEVICE::setupObject & YSE::DEVICE::setupObject::setOutput(const interfaceObject & out) {
  this->out = &out;
  return *this;
}

YSE::DEVICE::setupObject & YSE::DEVICE::setupObject::setSampleRate(double value) {
  sampleRate = value;
  return *this;
}

YSE::DEVICE::setupObject & YSE::DEVICE::setupObject::setBufferSize(int value) {
  bufferSize = value;
  return *this;
}

int YSE::DEVICE::setupObject::getOutputChannels() const {
  if (out == nullptr) return 0;
  return out->getOutputChannelNames().size();
}



