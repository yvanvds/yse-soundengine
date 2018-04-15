/*
  ==============================================================================

    ramp.cpp
    Created: 31 Jan 2014 2:55:23pm
    Author:  yvan

  ==============================================================================
*/

#include "ramp.hpp"
#include "../utils/misc.hpp"

YSE::DSP::ramp::ramp() : target(0), time(0), current(0), ticksLeft(0), reTarget(false)  {
  _1overN = 1.0f / getLength();
  _dspTickToMSEC = SAMPLERATE / (1000.0f * getLength());
}

YSE::DSP::ramp::ramp(YSE::DSP::ramp & source) {
  ticksLeft.store(source.ticksLeft);
  reTarget.store(source.reTarget);
  current.store(source.current);
  target.store(source.target);
  time.store(source.time);
  _1overN = 1.0f / getLength();
  _dspTickToMSEC = SAMPLERATE / (1000.0f * getLength());
}

YSE::DSP::ramp& YSE::DSP::ramp::set(Flt target, Int time) {
  if (time <= 0) {
    this->target = target;
    this->current = target;
    this->ticksLeft = 0;
    this->reTarget = false;
  }
  else {
    this->target = target;
    this->reTarget = true;
    this->time = static_cast<Flt>(time);
  }
  return (*this);
}

YSE::DSP::ramp& YSE::DSP::ramp::setIfNew(Flt target, Int time) {
  if (this->target != target) set(target, time);
  return *this;
}

YSE::DSP::ramp& YSE::DSP::ramp::stop() {
  target = current.load();
  ticksLeft = 0;
  reTarget = false;
  return (*this);
}

YSE::DSP::ramp& YSE::DSP::ramp::update() {
  if (reTarget) {
    nTicks = (Int)(time * _dspTickToMSEC);
    if (!nTicks) nTicks = 1;
    ticksLeft = nTicks;
    bigInc = (target - current) / static_cast<Flt>(nTicks);
    inc = _1overN * bigInc;
    reTarget = false;
  }

  l = getLength();
  ptr = getPtr();
  if (ticksLeft) {
    f = current;

    while (l--) *ptr++ = f, f += inc;
    current.store(current.load() + bigInc);
    ticksLeft--;
  }
  else {
    f = current = target.load();
    for (; l > 7; l -= 8, ptr += 8) {
      ptr[0] = f; ptr[1] = f; ptr[2] = f; ptr[3] = f;
      ptr[4] = f; ptr[5] = f; ptr[6] = f; ptr[7] = f;
    }
    while (l--) *ptr++ = f;
  }

  return (*this);
}

YSE::DSP::buffer & YSE::DSP::ramp::operator()() {
  return *this;
}

YSE::DSP::buffer & YSE::DSP::ramp::getSample() {
  return *this;
}

Flt YSE::DSP::ramp::getValue() {
  return current;
}

/************************************************************************/


void YSE::DSP::FastFadeIn(YSE::DSP::buffer & s, UInt length) {
  Clamp(length, 1u, s.getLength());
  Flt step = 1.0f / static_cast<Flt>(length);
  Flt multiplier = 0.0f;
  Flt * ptr = s.getPtr();
  for (UInt i = 0; i < length; i++) {
    *ptr++ *= multiplier;
    multiplier += step;
  }
}

/************************************************************************/

void YSE::DSP::FastFadeOut(YSE::DSP::buffer & s, UInt length) {
  Clamp(length, 1u, s.getLength());
  Flt step = 1.0f / static_cast<Flt>(length);
  Flt multiplier = 1.0f;
  Flt * ptr = s.getPtr();
  for (UInt i = 0; i < length; i++) {
    *ptr++ *= multiplier;
    multiplier -= step;
  }
  UInt leftOvers = s.getLength() - length;
  while (leftOvers--) *ptr++ = 0.0f;
}

/************************************************************************/

void YSE::DSP::ChangeGain(YSE::DSP::buffer & s, Flt currentGain, Flt newGain, UInt length) {
  if (currentGain == newGain) {
    s *= newGain;
    return;
  }

  Clamp(length, 1u, s.getLength());
  Flt step = (newGain - currentGain) / static_cast<Flt>(length);
  Flt multiplier = currentGain;
  Flt * ptr = s.getPtr();
  for (UInt i = 0; i < length; i++) {
    *ptr++ *= multiplier;
    multiplier += step;
  }
  UInt leftOvers = s.getLength() - length;
  for (; leftOvers > 7; leftOvers -= 8, ptr += 8) {
    ptr[0] *= newGain; ptr[1] *= newGain; ptr[2] *= newGain; ptr[3] *= newGain;
    ptr[4] *= newGain; ptr[5] *= newGain; ptr[6] *= newGain; ptr[7] *= newGain;
  }
  while (leftOvers--) *ptr++ *= newGain;
}


/************************************************************************/

YSE::DSP::lint::lint() : targetValue(0), currentValue(0), step(0), up(false), calculate(false) {
  stepSecond = SAMPLERATE / static_cast<Flt>(STANDARD_BUFFERSIZE);
}


Flt YSE::DSP::lint::operator()() {
  return currentValue;
}

Flt YSE::DSP::lint::target() {
  return targetValue;
}

YSE::DSP::lint& YSE::DSP::lint::update() {
  if (calculate) {
    currentValue = currentValue + step;
    if (up && currentValue >= targetValue) calculate = false;
    if (!up && currentValue <= targetValue) calculate = false;
  }
  return *this;
}

YSE::DSP::lint& YSE::DSP::lint::stop() {
  targetValue = currentValue.load();
  calculate = false;
  return *this;
}

YSE::DSP::lint& YSE::DSP::lint::setIfNew(Flt target, Int time) {
  if (targetValue != target) set(target, time);
  return *this;
}

YSE::DSP::lint& YSE::DSP::lint::set(Flt target, Int time) {
  if (time == 0) {
    currentValue = targetValue = target;
    calculate = false;
  }
  else {
    Flt difference = target - currentValue;
    if (difference != 0) {
      step = difference / static_cast<Flt>(stepSecond * (time / 1000.0f));
      up = difference > 0;
      calculate = true;
    }
    else {
      calculate = false;
    }
    targetValue = target;
  }
  return *this;
}

/************************************************************************/


