/*
  ==============================================================================

    log.h
    Created: 28 Jan 2014 4:13:37pm
    Author:  yvan

  ==============================================================================
*/

#ifndef LOG_H_INCLUDED
#define LOG_H_INCLUDED

#include <string>
#include "headers/enums.hpp"
#include "headers/defines.hpp"
#include "headers/types.hpp"

namespace YSE {
  class API log {
  public:
    /** set / get the current loglevel
        By default this is set to EL_DEBUG in debug mode
        and EL_ERRORS in release mode. Other modes are
        EL_WARNINGS and EL_NONE.
    */
    ERROR_LEVEL level(); log& level(ERROR_LEVEL value); 

    /** You can set a custom callback function here.
        This overwrites the normal logfile output
    */
    log& setCallback(void(*funcPtr)(const char *)); 

    /** set / get the current output file. By default 
        this file is called 'YSElog.txt' and will be
        placed in the work directory. (Dependant on OS)
    */
    std::string logfile(); log& logfile(const char * path);

    /** You can send your own log messages to the YSE log
        system. They will be printed with the keyword 'app message'.
        They will appear at Error loglevel.
    */
    log& message(const char * msg);
  };

  API log & Log();
}




#endif  // LOG_H_INCLUDED
