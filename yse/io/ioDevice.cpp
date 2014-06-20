/*
  ==============================================================================

    ioDevice.cpp
    Created: 12 Jun 2014 6:43:05pm
    Author:  yvan

  ==============================================================================
*/

#include "ioDevice.h"

YSE::IO::ioDevice::ioDevice(const std::wstring & name) 
: name(name)
, defaultDevice(false)
, callback(nullptr)
, isOpen(false)
, isStarted(false) {
}

void YSE::IO::ioDevice::setDescription(const std::wstring & description) {
  this->description = description;
}

std::wstring YSE::IO::ioDevice::getName() {
  return name;
}

std::wstring YSE::IO::ioDevice::getDescription() {
  return description;
}

bool YSE::IO::ioDevice::isDefault() {
  return defaultDevice;
}

void YSE::IO::ioDevice::setDefault(bool value) {
  defaultDevice = value;
}

std::wstring YSE::IO::ioDevice::getLastError() {
  return lastError;
}

void YSE::IO::ioDevice::setLastError(const std::wstring & message) {
  lastError = message;
}