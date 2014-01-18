#include "stdafx.h"
#if defined(USE_OPENSL)
#include "opensl.h"
#include "internal/channelimpl.h"
#include "system.hpp"
#include "internal/internalObjects.h"
#include "utils/error.hpp"
#include "device.hpp"

namespace YSE {
	UInt sampleRate = 44100;
};

Bool YSE::systemDevice::initialize() {
  init = open = started = false;

  return true;
  }

Bool YSE::systemDevice::openDevice(UInt ID, Int outChannels) {

	return true;
}


YSE::systemDevice::~systemDevice() {
	close();
}

void YSE::systemDevice::close() {
}

Int bufferPos = BUFFERSIZE;

std::string YSE::systemDevice::getError() {
	std::string result;
  return result;
}

YSE::device::device() {
	defaultIn = defaultOut = false;
	minLatency = maxLatency = preferredLatency = 0;
	sampleRate = 0;
}

Bool YSE::systemDevice::updateDevices() {

	return true;
}

Flt YSE::systemDevice::cpuLoad() {
	return 0;
}

#endif // defined USE_OPENSL
