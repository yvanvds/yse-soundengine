/*
  ==============================================================================

    fileFunctions.hpp
    Created: 29 Jul 2016 9:46:15pm
    Author:  yvan

  ==============================================================================
*/

#ifndef FILEFUNCTIONS_HPP_INCLUDED
#define FILEFUNCTIONS_HPP_INCLUDED

#include <string>

namespace YSE {
  std::string GetCurrentWorkingDirectory();
  bool FileExists(const std::string & name);
  bool IsPathAbsolute(const std::string & path);
  
}



#endif  // FILEFUNCTIONS_HPP_INCLUDED
