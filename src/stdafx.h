#pragma once
#define BOOST_LIB_DIAGNOSTIC
#include "headers/types.hpp"
#include "headers/defines.hpp"

#ifdef WINDOWS
#include <atomic>
#else // gnu version of atomic doesn't work with custom classes, so we use boost instead
#include <boost/atomic.hpp>
#endif

// android define is a bit long to type, so we define a shorter version
#ifdef __ANDROID__
#define ANDROID
#elif defined(__gnu_linux__)
#define LIN
#endif

#ifdef __APPLE__
  #ifdef TARGET_OS_MAC
    #define OSX
  #elif defined(TARGET_OS_IPHONE)
    #define IOS
  #endif // TARGET_OS_MAC
#endif // __APPLE__

#if defined(WINDOWS) || defined(OSX) || defined (LIN)
#define USE_PORTAUDIO
#elif defined(ANDROID)
#define USE_OPENSL
#endif // defined WINDOWS || OSX || LIN


// we need some extra effort to have an atomic version of YSE::Vec
// since custom class can only be atomic for visual studio at the moment

namespace YSE {
  struct Vec;

#ifdef WINDOWS
  typedef std::atomic<Vec> aVec;
#else
  typedef boost::atomic<Vec> aVec;
#endif
}
