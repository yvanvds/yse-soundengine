/*
  ==============================================================================

    logImplementation.cpp
    Created: 28 Jan 2014 4:13:48pm
    Author:  yvan

  ==============================================================================
*/

#include "logImplementation.h"

YSE::INTERNAL::logImplementation & YSE::INTERNAL::LogImpl() {
  static logImplementation impl;
  return impl;
}

YSE::INTERNAL::logImplementation::logImplementation() : funcPtr(nullptr) {
#if defined YSE_DEBUG
  level = EL_DEBUG;
  toDebugger = true;
#else
  level = EL_ERROR;
  toDebugger = false;
#endif

  // internal logger used if no funcPtr has been set
  File logFile = File::getCurrentWorkingDirectory().getChildFile("YSElog.txt");
  fileLogger = new FileLogger(logFile, "=== start of YSE log ===");
  
  // activate logger
  Logger::setCurrentLogger(this);
}

YSE::INTERNAL::logImplementation::~logImplementation() {
  Logger::writeToLog("=== end of YSE log ===");
  Logger::setCurrentLogger(nullptr);
}

YSE::ERROR_LEVEL YSE::INTERNAL::logImplementation::getLevel() {
  return level;
}

void YSE::INTERNAL::logImplementation::setLevel(YSE::ERROR_LEVEL value) {
  level = value;
}

void YSE::INTERNAL::logImplementation::setCallback(void(*funcPtr)(const char *)) {
  this->funcPtr = funcPtr;
}

void YSE::INTERNAL::logImplementation::setLogfile(const char * path) {
  fileLogger.release();
  File logFile(path);
  fileLogger = new FileLogger(logFile, "=== start of YSE log ===");
}

std::string YSE::INTERNAL::logImplementation::getLogfile() {
  return fileLogger->getLogFile().getFullPathName().toStdString();
}

void YSE::INTERNAL::logImplementation::logMessage(const String & message) {
  if (level == EL_NONE) return;

  if (funcPtr != nullptr) {
    funcPtr(message.toStdString().c_str());
  } else {
    fileLogger->logMessage(message);
  }
}

void YSE::INTERNAL::logImplementation::emit(ERROR_CODE value, const String & info) {
#if YSE_DEBUG
  if (Logger::getCurrentLogger() == nullptr) {
    /* This assertion is reached when you try to log something when the logger
    is not active. Most probably that means the program is shutting down.
    The log has already exited and you're still trying to send a message to it.
    */
    jassertfalse;
    return;
  }
#endif

  switch (level) {
    case EL_NONE: return;
    case EL_ERROR: if (value > E_WARNING_MESSAGES) return; break;
    case EL_WARNING: if (value > E_DEBUG_MESSAGES) return; break;
  }

  String  result = errorToText(value);
  if (info.length() > 0) result += " " + info;
  logMessage(result);
}

const char * YSE::INTERNAL::logImplementation::errorToText(YSE::ERROR_CODE value) {
  switch (value) {
    // errors
    case E_ERROR             : return "(Error) ";
    case E_MUTEX_UNSTABLE    : return "(Error) The audio thread locking has become unstable.";
    case E_AUDIODEVICE       : return "(Error) Audio device: ";
    case E_FILE_BYTE_COUNT   : return "(Error) SoundFile has wrong size.";
    case E_FILEREADER        : return "(Error) File reader: ";
    case E_TRACK_NOT_STARTED : return "(Error) Unable to start a music track.";
    case E_TRACK_TIMER_STOP  : return "(Error) Unable to stop track timer.";
    
    case E_APP_MESSAGE       : return "(App Message) ";
    
    // warnings
    case E_WARNING              : return "(Warning) ";
    case E_FILE_NOT_FOUND       : return "(Warning) Soundfile not found: ";
    case E_FILE_ERROR           : return "(Warning) Error while loading file: ";
    case E_SOUND_OBJECT_IN_USE  : return "(Warning) Sound object already in use when create was called.";
    case E_SOUND_OBJECT_NO_INIT : return "(Warning) Sound object used without creating it first.";
    case E_REVERB_NO_INIT       : return "(Warning) Reverb object is used without creating it";
    case E_CHANNEL_OBJECT_IN_USE: return "(Warning) Channel object already in use when create was called.";

    // debug
    case E_DEBUG        : return "(Debug) ";
    case E_SOUND_ADDED  : return "(Debug) Sound added to system.";
    case E_SOUND_WRONG  : return "(Debug) Object error with sound: ";
    case E_SOUND_DELETED: return "(Debug) Sound deleted from system.";
  };

  return "(Error) no YSE::ERROR_CODE is found for this error.";
}