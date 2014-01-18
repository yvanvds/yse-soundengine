#pragma once
#include "soundfile.h"
#include <boost/ptr_container/ptr_map.hpp>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>
#include "headers/types.hpp"

namespace YSE {
  // global object for file loading
  // used in system.cpp and soundfile.cpp
  std::deque<void *> & LoadList();
  void LoadSoundFiles();

  extern boost::ptr_map<std::string, soundFile> SoundFiles;
  extern Bool KeepLoading;

  extern std::condition_variable LoadSoundCondition;
}
