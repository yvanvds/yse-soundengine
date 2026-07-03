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
  /** @brief Current working directory of the process. */
  std::string GetCurrentWorkingDirectory();

  /** @brief Whether a file at ``name`` exists on disk. */
  bool FileExists(const std::string& name);

  /** @brief Whether ``path`` is absolute (vs. relative to the working directory). */
  bool IsPathAbsolute(const std::string& path);

} // namespace YSE

#endif // FILEFUNCTIONS_HPP_INCLUDED
