//
//  types.hpp
//  yse
//
//  Created by yvan vander sanden on 06/04/13.
//  Copyright (c) 2013 mute. All rights reserved.
//

#pragma once

#ifdef _WIN32
  #define PLATFORM(windows, unix) windows
  #define WINDOWS
  #ifdef _DLL_CREATION
    #define API __declspec(dllexport)
    #define EXTERN
  #else
    #define API __declspec(dllimport)
    #define EXTERN extern
  #endif
#elif defined __APPLE__
  #define PLATFORM(windows, unix) unix
  #include <TargetConditionals.h>
  #if TARGET_OS_IPHONE
    #define IOS
  #else
    #define MAC
  #endif
  #define API
  #define EXTERN
#elif defined ANDROID
  #define PLATFORM(windows, unix) unix
  #define API
  #define EXTERN
#elif defined __GNUC__
  #define PLATFORM(windows, unix) unix
  #define API
  #define EXTERN
  #define LINUX
#else
  #error Unsuported platform detected
#endif

