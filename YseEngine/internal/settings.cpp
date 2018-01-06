/*
  ==============================================================================

    settings.cpp
    Created: 2 Feb 2014 12:06:32pm
    Author:  yvan

  ==============================================================================
*/

#include "settings.h"

YSE::INTERNAL::settings & YSE::INTERNAL::Settings() {
  static settings s;
  return s;
}