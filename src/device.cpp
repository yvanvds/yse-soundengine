#include "stdafx.h"
#include "device.hpp"

#if defined(USE_PORTAUDIO)
  #include "backend/pa.h"
#elif defined(USE_OPENSL)
  #include "backend/opensl.h"
#endif // defined


Bool YSE::audioDevice::defaultIn() {
  return pimpl->defaultIn;
}

Bool YSE::audioDevice::defaultOut() {
  return pimpl->defaultOut;
}

const char * YSE::audioDevice::host() {
  return pimpl->host.c_str();
}

const char * YSE::audioDevice::name() {
  return pimpl->name.c_str();
}

Int YSE::audioDevice::inputs() {
  return pimpl->inChannels;
}

Int YSE::audioDevice::outputs() {
  return pimpl->outChannels;
}

UInt YSE::audioDevice::ID() {
  return pimpl->ID;
}
