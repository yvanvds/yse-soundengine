/*
  ==============================================================================

    misc.cpp
    Created: 16 Jun 2014 11:39:33pm
    Author:  yvan

  ==============================================================================
*/

#include "misc.hpp"

std::wstring YSE::StringToWString(const std::string& s)
{
  std::wstring temp(s.length(), L' ');
  std::copy(s.begin(), s.end(), temp.begin());
  return temp;
}