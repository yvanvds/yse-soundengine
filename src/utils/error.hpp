#pragma once
#include <string>
#include "../headers/enums.hpp"


namespace YSE {

  class API error {
  public:
    // set / get error level
    ERROR_LEVEL level(); error& level(ERROR_LEVEL value); // default: EL_ERRORS
    error& setCallback(void(*funcPtr)(const char *)); // write your own function to write errors to the screen or log file

    // for internal use
    void emit(ERROR_CODE value, const std::string & info = "");
    error();

  private:
    const std::string errorToText(ERROR_CODE value);
    void (*funcPtr)(const char *);
    ERROR_LEVEL _level;
  };

  extern API error Error;
}

