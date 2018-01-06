/*
  ==============================================================================

    interpolators.h
    Created: 10 Apr 2015 8:07:38pm
    Author:  yvan

  ==============================================================================
*/

#ifndef INTERPOLATORS_H_INCLUDED
#define INTERPOLATORS_H_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "misc.hpp"


namespace YSE {
  // This interpolator works with a time delta as update argument.
  // In every pass of the main loop, the update function should be called before
  // retrieving the value.
  class API linearInterpolator {
  public:
    linearInterpolator();
    linearInterpolator & set(Flt target, Flt time = 0);
    linearInterpolator & update(Flt timeDelta);
    Flt target();
    Flt operator()();

  private:
    Flt startValue, targetValue, currentValue;
    Flt totalTime, timeLeft;
  };

  // A template class for an object interpolator. The values in the objects are
  // not interpolated, but rather the current or target object is returned: if 
  // 'timeLeft' becomes smaller, the chance of getting the target object instead
  // of the current one becomes bigger.
  template <class TYPE> class objectInterpolator {
  public:
    objectInterpolator() : totalTime(0), timeLeft(0), _isSet(false) {}
    
    // Set a new target object. Be sure it has a valid copy constructor
    // (unless type is a pointer)
    objectInterpolator<TYPE> & set(TYPE obj, Flt time = 0) {
      if (!_isSet) {
        totalTime = timeLeft = 0;
        current = obj;
        _isSet = true;
      }
      else {
        totalTime = timeLeft = time;
        timeLeft > 0 ? target = obj : current = obj;
      }
      return *this;
    }

    // call this function once for every loop
    objectInterpolator<TYPE> & update(Flt timeDelta) {
      if (timeLeft > 0) {
        timeLeft -= timeDelta;
        if (timeLeft <= 0) {
          current = target;
          timeLeft = 0;
        }
      }
      return *this;
    }

    // get the object
    TYPE & operator()() {
      if (timeLeft > 0) {
        return RandomF(totalTime) > timeLeft ? target : current;
      }
      return current;
    }

    Bool isSet() { return _isSet; }
  private:
    TYPE current, target;
    Flt totalTime, timeLeft;
    Bool _isSet;
  };

}

#endif  // INTERPOLATORS_H_INCLUDED
