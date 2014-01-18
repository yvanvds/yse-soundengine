#include "stdafx.h"
#include "error.hpp"

YSE::error YSE::Error;

YSE::error::error() {
  funcPtr  = NULL     ;
  _level   = EL_ERRORS;
};

void YSE::error::emit(ERROR_CODE value, const std::string & info) {
  if (funcPtr == NULL) return;
  switch (_level) {
    case EL_NONE    : return;
    case EL_ERRORS  : if (value > E_WARNINGS) return; break;
    case EL_WARNINGS: if (value > E_DEBUG   ) return; break;
  }

  std::string result = errorToText(value);
  if (info.size() > 0) result += info;
  funcPtr(result.c_str());
}

YSE::ERROR_LEVEL YSE::error::level() {
  return _level;
}

YSE::error & YSE::error::level(ERROR_LEVEL value) {
  _level = value;
  return (*this);
}

YSE::error & YSE::error::setCallback(void(*funcPtr)(const char *)) {
  this->funcPtr = funcPtr;
  return (*this);
}

const std::string YSE::error::errorToText(ERROR_CODE value) {
  switch(value) {
    case E_MUTEX_UNSTABLE: return "The audio thread locking has become unstable.";
    case E_PORTAUDIO: return "Portaudio error: ";
    case E_SOUND_ADDED: return "Sound added to system.";
    case E_SOUND_WRONG: return "Object error with sound: ";
    case E_SOUND_DELETED: return "Sound deleted from system.";
    case E_FILE_NOT_FOUND: return "Soundfile not found: ";
    case E_FILE_ERROR: return "Error while loading file: ";
    case E_SOUND_OBJECT_IN_USE: return "Sound object already in use when create was called.";
    case E_SOUND_OBJECT_NO_INIT: return "Sound object used without creating it first.";
    case E_REVERB_NO_INIT: return "Reverb object is used without creating it";
    case E_CHANNEL_OBJECT_IN_USE: return "Channel object already in use when create was called.";
    case E_FILE_BYTE_COUNT: return "SoundFile has wrong size.";
    case E_LIBSNDFILE: return "Libsndfile error: ";
    case E_TRACK_NOT_STARTED: return "Unable to start a music track.";
    case E_TRACK_TIMER_STOP: return "Unable to stop track timer.";
  };
  return "Error: no explanation is found for this error.";
}
