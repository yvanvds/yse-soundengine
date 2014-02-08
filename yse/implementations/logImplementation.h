/*
  ==============================================================================

    logImplementation.h
    Created: 28 Jan 2014 4:13:48pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LOGIMPLEMENTATION_H_INCLUDED
#define LOGIMPLEMENTATION_H_INCLUDED

#include "JuceHeader.h"
#include "../headers/enums.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {
    class logImplementation : Logger {
    public:
      juce_DeclareSingleton(logImplementation, true)

      ERROR_LEVEL getLevel();
      void  setLevel(ERROR_LEVEL value);
      void  setCallback(void(*funcPtr)(const char *));
      std::string getLogfile();
      void  setLogfile(const char * path);

      void emit(ERROR_CODE value, const String & info = "");
      void logMessage(const String & message);

      logImplementation();
      ~logImplementation();
    private:
      const char * errorToText(ERROR_CODE value);
      void(*funcPtr)(const char *);
      ERROR_LEVEL level;
      ScopedPointer<FileLogger> fileLogger;
      Bool toDebugger;
    };
  }
}



#endif  // LOGIMPLEMENTATION_H_INCLUDED
