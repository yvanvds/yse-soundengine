/*
  ==============================================================================

    ioDeviceType.cpp
    Created: 12 Jun 2014 8:09:31pm
    Author:  yvan

  ==============================================================================
*/

#include "ioDeviceType.h"
#include "../headers/defines.hpp"

YSE::IO::ioDeviceType::ioDeviceType(const std::wstring & name) : typeName(name) {
}

std::shared_ptr<YSE::IO::ioDeviceType> YSE::IO::ioDeviceType::create() {
  #if (YSE_WINDOWS && YSE_WASAPI) 
  return YSE::IO::createWASAPI();
  #endif
}


#if (YSE_WINDOWS && YSE_WASAPI) 
std::shared_ptr<YSE::IO::ioDeviceType> YSE::IO::createWASAPI() {
  return nullptr;
}
#endif