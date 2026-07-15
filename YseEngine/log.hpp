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

  /**
   *  @brief Base class for custom log sinks.
   *
   *  Subclass and override ``AddMessage`` to route log output somewhere other
   *  than the default log file — for example an in-game console or a
   *  third-party telemetry system. Register the instance with
   *  ``log::setHandler``.
   */
  class API logHandler {
  public:
    /** @brief Called by the engine for every log message. Default implementation discards it. */
    virtual void AddMessage(const std::string&) {}
    virtual ~logHandler() {}
  };

  /**
   *  @brief Singleton logging facility.
   *
   *  By default the engine writes messages to a text file in the working
   *  directory. Use ``setLogfile`` to redirect that file, or ``setHandler``
   *  to bypass the file entirely and feed log lines into your own sink.
   *
   *  @note Logging is only active between ``System().init()`` and
   *        ``System().close()``.
   *  @see YSE::Log
   */
  class API log {
  public:
    /** @brief Send an application-level message to the YSE log.
     *
     *  The message is tagged ``app message`` and emitted at error log level
     *  so it survives filters set above ``EL_DEBUG``.
     */
    log& sendMessage(const char* msg);

    /** @brief Set the active log level.
     *
     *  Defaults to ``EL_DEBUG`` in debug builds and ``EL_ERROR`` in release
     *  builds. Messages above the chosen level are dropped.
     */
    log& setLevel(ERROR_LEVEL value);

    /** @brief Current log level. */
    ERROR_LEVEL getLevel();

    /** @brief Install a custom log handler.
     *
     *  Replaces the default file-based sink. Pass ``nullptr`` to restore the
     *  default behaviour.
     */
    log& setHandler(logHandler* handler);

    /** @brief Change the path of the default log file.
     *
     *  Defaults to ``YSElog.txt`` in the process working directory. Has no
     *  effect after a custom handler has been installed via ``setHandler``.
     */
    log& setLogfile(const char* path);

    /** @brief Current log file path. */
    const char* getLogfile();
  };

  /** @brief Access the singleton logging object. */
  API log& Log();
} // namespace YSE

#endif // LOG_H_INCLUDED
