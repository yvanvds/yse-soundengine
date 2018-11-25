/*
  ==============================================================================

    device.cpp
    Created: 10 Apr 2014 2:43:14pm
    Author:  yvan

  ==============================================================================
*/

#include "deviceInterface.hpp"
#include "internalHeaders.h"


YSE::device::device() {
}

YSE::device & YSE::device::setName(const std::string & name) {
  this->name = name;
  return *this;
}

const std::string & YSE::device::getName() const {
  return name;
}

YSE::device & YSE::device::setTypeName(const std::string & typeName) {
  this->typeName = typeName;
  return *this;
}

const std::string & YSE::device::getTypeName() const {
  return typeName;
}

YSE::device & YSE::device::addInputChannelName(const std::string & name) {
  inputChannelNames.emplace_back(name);
  return *this;
}

YSE::device & YSE::device::addOutputChannelName(const std::string & name) {
  outputChannelNames.emplace_back(name);
  return *this;
}

YSE::device & YSE::device::addAvailableSampleRate(double sr) {
  sampleRates.push_back(sr);
  return *this;
}

YSE::device & YSE::device::addAvailableBufferSize(int bs) {
  bufferSizes.push_back(bs);
  return *this;
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

unsigned int YSE::device::getNumOutputChannelNames() const {
  return (UInt)outputChannelNames.size();
}

const std::string & YSE::device::getOutputChannelName(unsigned int nr) const {
  return outputChannelNames[nr];
}

unsigned int YSE::device::getNumInputChannelNames() const {
  return (UInt)inputChannelNames.size();
}

const std::string & YSE::device::getInputChannelName(unsigned int nr) const {
  return inputChannelNames[nr];
}

unsigned int YSE::device::getNumAvailableSampleRates() const {
  return (UInt)sampleRates.size();
}

double YSE::device::getAvailableSampleRate(unsigned int nr) const {
  return sampleRates[nr];
}

unsigned int YSE::device::getNumAvailableBufferSizes() const {
  return (UInt)bufferSizes.size();
}

int YSE::device::getAvailableBufferSize(unsigned int nr) const {
  return bufferSizes[nr];
}

YSE::device & YSE::device::setDefaultBufferSize(int value) {
  defaultBufferSize = value;
  return *this;
}

int YSE::device::getDefaultBufferSize() const {
  return defaultBufferSize;
}

YSE::device & YSE::device::setOutputLatency(int value) {
  outputLatency = value;
  return *this;
}

int YSE::device::getOutputLatency() const {
  return outputLatency;
}

YSE::device & YSE::device::setInputLatency(int value) {
  inputLatency = value;
  return *this;
}

int YSE::device::getInputLatency() const {
  return inputLatency;
}

YSE::device & YSE::device::setID(int value) {
  ID = value;
  return *this;
}

int YSE::device::getID() const {
  return ID;
}
