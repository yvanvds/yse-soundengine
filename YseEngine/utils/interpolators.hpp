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

  /**
   *  @brief Block-rate linear interpolator driven by an external time delta.
   *
   *  Call ``set`` to start interpolating toward a target over ``time`` seconds,
   *  then call ``update(timeDelta)`` once per frame and ``operator()`` to read
   *  the current value.
   */
  class API linearInterpolator {
  public:
    linearInterpolator();

    /** @brief Start a new ramp toward ``target`` over ``time`` seconds. */
    linearInterpolator & set(Flt target, Flt time = 0);

    /** @brief Advance the interpolator by ``timeDelta`` seconds. */
    linearInterpolator & update(Flt timeDelta);

    /** @brief Current target value. */
    Flt target();

    /** @brief Current interpolated value. */
    Flt operator()();

  private:
    Flt startValue, targetValue, currentValue;
    Flt totalTime, timeLeft;
  };

  /**
   *  @brief Probabilistic cross-fade between two objects.
   *
   *  Unlike ``linearInterpolator``, this template does NOT blend the objects
   *  themselves — it returns either the current or the target object on each
   *  call. As ``timeLeft`` decreases, the probability of returning the target
   *  rises until it reaches 1.0 at ``timeLeft == 0``. Useful for non-blendable
   *  state (pointers, discrete enums, etc.).
   */
  template <class TYPE> class objectInterpolator {
  public:
    objectInterpolator() : totalTime(0), timeLeft(0), _isSet(false) {}

    /**
     *  @brief Set a new target object.
     *
     *  ``TYPE`` must be copy-constructible unless it is itself a pointer.
     *  Setting a target for the first time replaces the current value
     *  immediately; subsequent calls start a cross-fade.
     */
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

    /** @brief Advance by ``timeDelta`` seconds. */
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

    /** @brief Return either the current or target object, weighted by how far the cross-fade has progressed. */
    TYPE & operator()() {
      if (timeLeft > 0) {
        return RandomF(totalTime) > timeLeft ? target : current;
      }
      return current;
    }

    /** @brief Whether any object has been set. */
    Bool isSet() { return _isSet; }
  private:
    TYPE current, target;
    Flt totalTime, timeLeft;
    Bool _isSet;
  };

}

#endif  // INTERPOLATORS_H_INCLUDED
