/*
  ==============================================================================

    logImplementation.h
    Created: 28 Jan 2014 4:13:48pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LOGIMPLEMENTATION_H_INCLUDED
#define LOGIMPLEMENTATION_H_INCLUDED

#include "../headers/enums.hpp"
#include "../headers/types.hpp"
#include "../log.hpp"
#include <string>
#include <fstream>

namespace YSE {
  namespace INTERNAL {
    class logImplementation {
    public:

      ERROR_LEVEL getLevel();
      void  setLevel(ERROR_LEVEL value);
      void  setHandler(logHandler * handler);
      const std::string & getLogfile();
      void  setLogfile(const char * path);

      void emit(ERROR_CODE value, const std::string & info = "");
      void logMessage(const std::string & message);

      logImplementation();
      ~logImplementation();
    private:
      const char * errorToText(ERROR_CODE value);
      logHandler * handler;
      ERROR_LEVEL level;
	  std::ofstream logFile;
	  std::string logFileName;
      Bool toDebugger;
    };

    logImplementation & LogImpl();
  }
}



#endif  // LOGIMPLEMENTATION_H_INCLUDED
