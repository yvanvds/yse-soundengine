/*
  ==============================================================================

    ramp.h
    Created: 31 Jan 2014 2:55:23pm
    Author:  yvan

  ==============================================================================
*/

#ifndef RAMP_H_INCLUDED
#define RAMP_H_INCLUDED

#include "../headers/defines.hpp"
#include "buffer.hpp"

namespace YSE {

  namespace DSP {

    /**
     *  @brief Sample-accurate ramp generator.
     *
     *  Smoothly slews toward a target value over a time. Use as a smoothing
     *  layer in front of parameters that would otherwise click (volume, pan,
     *  filter cutoff). Inherits from ``buffer`` because each ``operator()``
     *  fills its own audio block.
     */
    class API ramp : public buffer {
    public:
      /** @brief Start a new ramp toward ``target`` over ``time`` milliseconds. */
      ramp& set(Flt target, Int time = 0);

      /** @brief Like ``set``, but only re-targets if ``target`` differs from the current target. */
      ramp& setIfNew(Flt target, Int time = 0);

      /** @brief Stop ramping; current value becomes the new target. */
      ramp& stop();

      /** @brief Advance one block. Call once per DSP tick. */
      ramp& update();

      /** @brief Most recent ramp block. */
      YSE::DSP::buffer & operator()();

      /** @brief Same as ``operator()``. */
      YSE::DSP::buffer & getSample();

      /** @brief Current scalar ramp value (block-rate). */
      Flt    getValue();

      ramp();
      ramp(ramp &);

    private:
      aFlt target;
      aFlt time;
      aFlt current;
      aInt ticksLeft;
      aBool reTarget;

      Flt _1overN;
      Flt _dspTickToMSEC;
      Flt inc, bigInc;
      Int nTicks;
      Int l;
      Flt * ptr;
      Flt f;
    };

    /**
     *  @brief Scalar linear interpolator.
     *
     *  Like ``ramp`` but block-rate only: advances by one step per
     *  ``update()`` call rather than producing a sample-accurate buffer.
     *  Cheaper when you don't need per-sample smoothing.
     */
    class API lint {
    public:
      /** @brief Start a new ramp toward ``target`` over ``time`` milliseconds. */
      lint& set(Flt target, Int time);

      /** @brief Like ``set``, but only re-targets if ``target`` differs. */
      lint& setIfNew(Flt target, Int time);

      /** @brief Freeze the value at the current position. */
      lint& stop();

      /** @brief Advance one block. Call once per DSP tick. */
      lint& update();

      /** @brief Current target value. */
      Flt target();

      /** @brief Current value. */
      Flt operator()();
      lint();
    private:
      aFlt targetValue, currentValue, step;
      aBool up, calculate;
      Flt stepSecond;
    };

    /**
     *  @brief Cheap in-place fade-in over the first ``length`` samples of ``s``.
     *  @note ``length`` must not exceed ``s.getLength()``.
     */
    API void FastFadeIn(YSE::DSP::buffer & s, UInt length);

    /**
     *  @brief Cheap in-place fade-out over the first ``length`` samples of ``s``.
     *  @note ``length`` must not exceed ``s.getLength()``.
     */
    API void FastFadeOut(YSE::DSP::buffer & s, UInt length);

    /**
     *  @brief Linearly slew the gain of ``s`` from ``currentGain`` to ``newGain``.
     *
     *  Applied to the first ``length`` samples. Used internally to avoid
     *  zipper noise on volume changes.
     */
    API void ChangeGain(YSE::DSP::buffer & s, Flt currentGain, Flt newGain, UInt length);

  }
}



#endif  // RAMP_H_INCLUDED
