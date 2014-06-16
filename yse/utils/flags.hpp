/*
  ==============================================================================

    flags.h
    Created: 14 Jun 2014 12:44:32pm
    Author:  yvan

  ==============================================================================
*/

#ifndef FLAGS_H_INCLUDED
#define FLAGS_H_INCLUDED

#include <vector>
#include "../headers/types.hpp"

namespace YSE {

  // a class which can hold an unlimited amount of boolean values
  class flags : public std::vector<bool> {
  public:
    // set a range of values. If a position does not exist yet, it
    // will be created. If that position is not part of the specified range,
    // it will be false
    void setRange(UInt pos, UInt elements, bool value);
    
    // set a value at a fixed position. If the position does not exist yet,
    // this position and all the positions before it will be created. The position
    // gets the value. The other positions that might be created will be false
    void setValue(UInt pos, bool value);

    // set all values from this position onwards to the value. If startPos is past
    // the current end of the container, no value will be added
    void setFrom(UInt pos, bool value);

    // removes all values that are false from the end of the container
    // the last element will now be the biggest element which is true
    void trimEnd();

    // remove all values from this position onwards, no matter what their value is
    void removeFrom(UInt pos);

    // get the highest position that is set to true
    int getHighestBit();
  };

}



#endif  // FLAGS_H_INCLUDED
