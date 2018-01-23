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
#include "headers/defines.hpp"
#include "headers/enums.hpp"

namespace YSE {
  // a base class with a virtual method which can be passed to the log
  // class for custom output
  class API logHandler {
  public:
    virtual void AddMessage(const std::string & message) {}
    virtual ~logHandler() {}
  };

  /**
      A singleton class for logging. Normally this will write messages to a file when asked for. The behaviour can be
        overwritten though. Keep in mind that the logging system is
        only available between System().Init() and System().close().
  */
  class API log {
  public:
      
    /** You can send your own log messages to the YSE log
        system. They will be printed with the keyword 'app message'.
        They will appear at Error loglevel.
    */
    log& sendMessage(const char * msg);
      
    /** set the current loglevel.
        By default this is set to EL_DEBUG in debug mode
        and EL_ERRORS in release mode. Other modes are
        EL_WARNINGS and EL_NONE.
    */
    log& setLevel(ERROR_LEVEL value);
      
    /** Get the current loglevel.
    */
    ERROR_LEVEL getLevel();
      
    /** You can set a custom log handler here.
        This overwrites the normal logfile output
    */
    log& setHandler(logHandler * handler); 

    /** set the current output file. By default
        this file is called 'YSElog.txt' and will be
        placed in the work directory. (Dependant on OS)
    */
    log& setLogfile(const char * path);

    /** Get the current output file.
    */
    const char * getLogfile();
  };
  
  /**
      A functor to retrieve the Logging object.
  */
  API log & Log();
}


#endif  // LOG_H_INCLUDED
