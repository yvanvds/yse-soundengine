/*
  ==============================================================================

    flags.cpp
    Created: 14 Jun 2014 12:44:32pm
    Author:  yvan

  ==============================================================================
*/

#include "flags.hpp"
#include "../headers/types.hpp"

void YSE::flags::setRange(UInt pos, UInt elements, bool value) {
  while (pos > size()) {
    push_back(false);
  }
  for (UInt i = pos; i < pos + elements; i++) {
    if (i == size()) {
      push_back(value);
    }
    else {
      (*this)[i] = value;
    }
  }
}

void YSE::flags::setValue(UInt pos, bool value) {
  while (pos > size()) {
    push_back(false);
  }
  if (pos == size()) {
    push_back(value);
  }
  else {
    (*this)[pos] = value;
  }
}

void YSE::flags::setFrom(UInt pos, bool value) {
  while (pos < size()) {
    (*this)[pos] = value;
  }
}

void YSE::flags::trimEnd() {
  while (!back()) {
    pop_back();
  }
}

void YSE::flags::removeFrom(UInt pos) {
  while (pos >= size()) {
    pop_back();
  }
}

