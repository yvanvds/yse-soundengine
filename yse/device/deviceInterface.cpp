/*
  ==============================================================================

    device.cpp
    Created: 10 Apr 2014 2:43:14pm
    Author:  yvan

  ==============================================================================
*/

#include "deviceInterface.hpp"
#include "../internalHeaders.h"


YSE::DEVICE::interfaceObject::interfaceObject(juce::AudioIODevice * pimpl) : pimpl(pimpl) {
  StringArray in = pimpl->getInputChannelNames();
  for (int i = 0; i < in.size(); i++) {
    inputChannelNames.emplace_back(in[i].getCharPointer());
  }
  StringArray out = pimpl->getOutputChannelNames();
  for (int i = 0; i < out.size(); i++) {
    outputChannelNames.emplace_back(out[i].getCharPointer());
  }
  Array<double> sr = pimpl->getAvailableSampleRates();
  for (int i = 0; i < sr.size(); i++) {
    sampleRates.push_back(sr[i]);
  }
  Array<int> bs = pimpl->getAvailableBufferSizes();
  for (int i = 0; i < bs.size(); i++) {
    bufferSizes.push_back(bs[i]);
  }
}

const char * YSE::device::getName() const {
  return pimpl->getName().getCharPointer();
}

const char * YSE::device::getTypeName() const {
  return pimpl->getTypeName().getCharPointer();
}

const std::vector<std::string> & YSE::device::getOutputChannelNames() const {
  return outputChannelNames;
}

const std::vector<std::string> & YSE::device::getInputChannelNames() const {
  return inputChannelNames;
}

const std::vector<double> & YSE::device::getAvailableSampleRates() const {
  return sampleRates;
}

const std::vector<int> & YSE::device::getAvailableBufferSizes() const {
  return bufferSizes;
}

UInt YSE::device::getNumOutputChannelNames() const {
  return outputChannelNames.size();
}

const char * YSE::device::getOutputChannelName(UInt nr) const {
  return outputChannelNames[nr].c_str();
}

UInt YSE::device::getNumInputChannelNames() const {
  return inputChannelNames.size();
}

const char * YSE::device::getInputChannelName(UInt nr) const {
  return inputChannelNames[nr].c_str();
}

UInt YSE::device::getNumAvailableSampleRates() const {
  return sampleRates.size();
}

double YSE::device::getAvailableSampleRate(UInt nr) const {
  return sampleRates[nr];
}

UInt YSE::device::getNumAvailableBufferSizes() const {
  return bufferSizes.size();
}

Int YSE::device::getAvailableBufferSize(UInt nr) const {
  return bufferSizes[nr];
}

int YSE::device::getDefaultBufferSize() const {
  return pimpl->getDefaultBufferSize();
}

int YSE::device::getOutputLatency() const {
  return pimpl->getOutputLatencyInSamples();
}

int YSE::device::getInputLatency() const {
  return pimpl->getInputLatencyInSamples();
}

