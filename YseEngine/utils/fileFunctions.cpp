/*
  ==============================================================================

    fileFunctions.cpp
    Created: 29 Jul 2016 9:46:15pm
    Author:  yvan

  ==============================================================================
*/

#include "fileFunctions.hpp"
#include "../headers/defines.hpp"
#include <fstream>

#if defined YSE_WINDOWS
#include <filesystem>
#include <system_error>

// The process's working directory, as the POSIX branch reports it. This used to
// return the executable's folder (GetModuleFileNameA with the file name cut
// off), so on Windows every relative sound or patcher file name resolved next to
// the host .exe instead of where the host had chdir'ed to (#1249).
// std::filesystem::current_path reads it with GetCurrentDirectoryW, so it is
// neither cut short at MAX_PATH nor limited to the ANSI code page. The narrow
// form is returned because every consumer opens the joined path with a narrow
// API (std::ifstream, the sound file reader), which reads it in that code page.
// A directory that code page cannot spell has no narrow absolute form at all;
// "." still names it to those APIs, which resolve a relative path against the
// real working directory themselves.
std::string YSE::GetCurrentWorkingDirectory() {
  std::error_code ec;
  const std::filesystem::path cwd = std::filesystem::current_path(ec);
  if (ec || cwd.empty()) return ".";
  try {
    return cwd.string();
  } catch (...) {
    return ".";
  }
}

bool YSE::IsPathAbsolute(const std::string& p) {
  std::filesystem::path path(p);
  return path.is_absolute();
}

#else
#include <limits.h>
#include <unistd.h>

std::string YSE::GetCurrentWorkingDirectory() {
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/cwd", result, PATH_MAX);
  return std::string(result, (count > 0) ? count : 0);
}

/// on linux this is a really simple check. sorry
bool YSE::IsPathAbsolute(const std::string& p) {
  // TODO_2018 std::filesystem::path path(p);
  // TODO_2018 return path.is_absolute();
  return (p[0] == '/');
}

#endif

///
bool YSE::FileExists(const std::string& name) {
  std::ifstream file(name);
  return file.good();
}
