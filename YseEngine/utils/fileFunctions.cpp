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
#include <Windows.h>
#include <filesystem>


std::string YSE::GetCurrentWorkingDirectory() {
  char buffer[MAX_PATH];
  GetModuleFileNameA(NULL, buffer, MAX_PATH);
  std::string::size_type pos = std::string(buffer).find_last_of("\\/");
  return std::string(buffer).substr(0, pos);
}

bool YSE::IsPathAbsolute(const std::string & p) {
  std::experimental::filesystem::path path(p);
  return path.is_absolute();
}

#else
#include <limits.h>
#include <unistd.h>

std::string YSE::GetCurrentWorkingDirectory()
{
  char result[PATH_MAX];
  ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
  return std::string(result, (count > 0) ? count : 0);
}
#endif

bool YSE::FileExists(const std::string & name) {
  std::ifstream file(name);
  return file.good();
}

#if YSE_ANDROID 

bool YSE::IsPathAbsolute(const std::string & p) {
  return false;
}

#endif
