/*
  ==============================================================================

    virtualFinder.h
    Created: 8 Mar 2014 8:13:39pm
    Author:  yvan

  ==============================================================================
*/

#ifndef VIRTUALFINDER_H_INCLUDED
#define VIRTUALFINDER_H_INCLUDED

#include <vector>
#include "../headers/types.hpp"

namespace YSE {

  class virtualFinder {
  public:
    void reset();
    void add(Flt value);
    void setLimit(Int value);
    Int getLimit();
    void calculate();
    bool inRange(Flt value); // check if this value is ok according to the last calculations
    virtualFinder(Int resolution); // argument is the initial resolution. This can be adjusted internally when needed.

  private:
    Int resolution;
    aInt limit; // the number of entries that should be in range
    Int currentLimit;
    Flt range; // the calculated range
    Flt maximum; // the biggest value since last reset
    Flt calculatedMax; // the biggest value before last reset (we can assume this is about the same as the new value will be)
    Int entries; // how many entries are added since reset
    std::vector<Int> bin;
  };

  virtualFinder & VirtualSoundFinder();
}



#endif  // VIRTUALFINDER_H_INCLUDED
