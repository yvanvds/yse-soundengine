/*
  ==============================================================================

    log.cpp
    Created: 28 Jan 2014 4:13:37pm
    Author:  yvan

  ==============================================================================
*/

#include "internalHeaders.h"

YSE::log & YSE::Log() {
  static log l;
  return l;
}


YSE::ERROR_LEVEL YSE::log::getLevel() {
  return INTERNAL::Global().getLog().getLevel();
}

YSE::log & YSE::log::setLevel(YSE::ERROR_LEVEL value) {
  INTERNAL::Global().getLog().setLevel(value);
  return (*this);
}

YSE::log & YSE::log::setCallback(void(*funcPtr)(const char *)) {
  INTERNAL::Global().getLog().setCallback(funcPtr);
  return (*this);
}

std::string YSE::log::getLogfile() {
  return INTERNAL::Global().getLog().getLogfile();
}

YSE::log & YSE::log::setLogfile(const char * path) {
  INTERNAL::Global().getLog().setLogfile(path);
  return (*this);
}

YSE::log & YSE::log::sendMessage(const char * msg) {
  INTERNAL::Global().getLog().emit(E_APP_MESSAGE, msg);
  return (*this);
}