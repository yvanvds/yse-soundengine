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
  return INTERNAL::LogImpl().getLevel();
}

YSE::log & YSE::log::setLevel(YSE::ERROR_LEVEL value) {
  INTERNAL::LogImpl().setLevel(value);
  return (*this);
}

YSE::log & YSE::log::setHandler(logHandler * handler) {
  INTERNAL::LogImpl().setHandler(handler);
  return (*this);
}

const char * YSE::log::getLogfile() {
  return INTERNAL::LogImpl().getLogfile().c_str();
}

YSE::log & YSE::log::setLogfile(const char * path) {
  INTERNAL::LogImpl().setLogfile(path);
  return (*this);
}

YSE::log & YSE::log::sendMessage(const char * msg) {
  INTERNAL::LogImpl().emit(E_APP_MESSAGE, msg);
  return (*this);
}