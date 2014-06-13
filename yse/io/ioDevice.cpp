/*
  ==============================================================================

    ioDevice.cpp
    Created: 12 Jun 2014 6:43:05pm
    Author:  yvan

  ==============================================================================
*/

#include "ioDevice.h"

YSE::IO::ioDevice::ioDevice(const std::string & name, const std::string & typeName)
: name(name), typeName(typeName) {}

YSE::IO::ioDevice::~ioDevice() {}

bool YSE::IO::ioDevice::enablePreprocessing(bool value) {
  return false;
}