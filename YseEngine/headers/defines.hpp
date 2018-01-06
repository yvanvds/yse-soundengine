/*
  ==============================================================================

    defines.hpp
    Created: 27 Jan 2014 7:16:58pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DEFINES_HPP_INCLUDED
#define DEFINES_HPP_INCLUDED

#include <vector>

#if (defined (_WIN32) || defined (_WIN64))
  #define       YSE_WIN32 1
  #define       YSE_WINDOWS 1
#elif defined (__ANDROID__)
  #undef        YSE_ANDROID
  #define       YSE_ANDROID 1
#elif defined (LINUX) || defined (__linux__)
  #define     YSE_LINUX 1
#elif defined (__APPLE_CPP__) || defined(__APPLE_CC__)
  #define Point CarbonDummyPointName // (workaround to avoid definition of "Point" by old Carbon headers)
  #define Component CarbonDummyCompName
  #include <CoreFoundation/CoreFoundation.h> // (needed to find out what platform we're using)
  #undef Point
  #undef Component

  #if TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR
    #define     YSE_IPHONE 1
    #define     YSE_IOS 1
  #else
    #define     YSE_MAC 1
  #endif

#elif defined (__FreeBSD__)
  #define       YSE_BSD 1
#else
  #error "Unknown platform!"
#endif

//==============================================================================
#if YSE_WINDOWS
#ifdef _MSC_VER
#ifdef _WIN64
#define YSE_64BIT 1
#else
#define YSE_32BIT 1
#endif
#endif

#ifdef _DEBUG
#define YSE_DEBUG 1
#endif

#ifdef __MINGW32__
#define YSE_MINGW 1
#ifdef __MINGW64__
#define YSE_64BIT 1
#else
#define YSE_32BIT 1
#endif
#endif

/** If defined, this indicates that the processor is little-endian. */
#define YSE_LITTLE_ENDIAN 1

#define YSE_INTEL 1
#endif

//==============================================================================
#if YSE_MAC || YSE_IOS

#if defined (DEBUG) || defined (_DEBUG) || ! (defined (NDEBUG) || defined (_NDEBUG))
#define YSE_DEBUG 1
#endif

#if ! (defined (DEBUG) || defined (_DEBUG) || defined (NDEBUG) || defined (_NDEBUG))
#warning "Neither NDEBUG or DEBUG has been defined - you should set one of these to make it clear whether this is a release build,"
#endif

#ifdef __LITTLE_ENDIAN__
#define YSE_LITTLE_ENDIAN 1
#else
#define YSE_BIG_ENDIAN 1
#endif

#ifdef __LP64__
#define YSE_64BIT 1
#else
#define YSE_32BIT 1
#endif

#if defined (__ppc__) || defined (__ppc64__)
#define YSE_PPC 1
#elif defined (__arm__) || defined (__arm64__)
#define YSE_ARM 1
#else
#define YSE_INTEL 1
#endif

#if YSE_MAC && MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_4
#error "Building for OSX 10.3 is no longer supported!"
#endif

#if YSE_MAC && ! defined (MAC_OS_X_VERSION_10_5)
#error "To build with 10.4 compatibility, use a 10.5 or 10.6 SDK and set the deployment target to 10.4"
#endif
#endif

//==============================================================================
#if YSE_LINUX || YSE_ANDROID

#ifdef _DEBUG
#define YSE_DEBUG 1
#endif

// Allow override for big-endian Linux platforms
#if defined (__LITTLE_ENDIAN__) || ! defined (YSE_BIG_ENDIAN)
#define YSE_LITTLE_ENDIAN 1
#undef YSE_BIG_ENDIAN
#else
#undef YSE_LITTLE_ENDIAN
#define YSE_BIG_ENDIAN 1
#endif

#if defined (__LP64__) || defined (_LP64)
#define YSE_64BIT 1
#else
#define YSE_32BIT 1
#endif

#ifdef __arm__
#define YSE_ARM 1
#elif __MMX__ || __SSE__ || __amd64__
#define YSE_INTEL 1
#endif
#endif

//==============================================================================
// Compiler type macros.

#ifdef __clang__
#define YSE_CLANG 1
#define YSE_GCC 1
#elif defined (__GNUC__)
#define YSE_GCC 1
#elif defined (_MSC_VER)
#define YSE_MSVC 1

#if _MSC_VER < 1500
#define YSE_VC8_OR_EARLIER 1

#if _MSC_VER < 1400
#define YSE_VC7_OR_EARLIER 1

#if _MSC_VER < 1300
#warning "MSVC 6.0 is no longer supported!"
#endif
#endif
#endif

#if YSE_64BIT || ! YSE_VC7_OR_EARLIER
#define YSE_USE_INTRINSICS 1
#endif
#else
#error unknown compiler
#endif


//==============================================================================
// DLL building settings on Windows
#if YSE_MSVC
  #ifdef YSE_DLL_BUILD
    #define API __declspec (dllexport)
    #define EXTERN
    #pragma warning (disable: 4251)
  #elif defined (YSE_DLL)
    #define API __declspec (dllimport)
    #define EXTERN extern
    #pragma warning (disable: 4251)
  #endif
#endif

//==============================================================================
// DLL building on mac
#if YSE_MAC
  #ifdef YSE_DLL_BUILD
    #define API __attribute__((visibility("default")))
    #define EXTERN
  #elif defined(YSE_DLL)
    #define API
    #define EXTERN extern
  #endif
#endif

//==============================================================================
#ifndef API
#define API   /**< This macro is added to all public class declarations. */
#define EXTERN
#endif

#if defined YSE_MAC || YSE_IOS
#include <TargetConditionals.h>
#endif

#if defined YSE_WINDOWS
#define PLATFORM(windows, unix) windows
#else
#define PLATFORM(windows, unix) unix
#endif

//===============================================================================

#define MULTICHANNELBUFFER std::vector<YSE::DSP::buffer>

#endif  // DEFINES_HPP_INCLUDED
