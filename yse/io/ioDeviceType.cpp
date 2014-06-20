/*
  ==============================================================================

    ioDeviceType.cpp
    Created: 12 Jun 2014 8:09:31pm
    Author:  yvan

  ==============================================================================
*/

#include <memory>
#include "ioDeviceType.h"
#include "../headers/defines.hpp"

YSE::IO::ioDeviceType::ioDeviceType(const std::wstring & name) : typeName(name) {
}

std::shared_ptr<YSE::IO::ioDeviceType> YSE::IO::ioDeviceType::Create(DEVICETYPE type) {
  #if (YSE_WINDOWS && YSE_WASAPI)
  if (type == WASAPI) return YSE::IO::createWASAPI();
  #endif

#if (YSE_WINDOWS && YSE_DIRECTSOUND) 
  if (type == DIRECTSOUND) return YSE::IO::createDirectSound();
#endif
  return nullptr;
}


#if (YSE_WINDOWS && YSE_WASAPI) 
std::shared_ptr<YSE::IO::ioDeviceType> YSE::IO::createWASAPI() {
  return nullptr;
}
#endif

#if (YSE_WINDOWS && YSE_DIRECTSOUND) 
#include "native/directsound/directsoundDeviceType.h"

std::shared_ptr<YSE::IO::ioDeviceType> YSE::IO::createDirectSound() {
  return std::shared_ptr<ioDeviceType>(new directSoundDeviceType);
}
#endif