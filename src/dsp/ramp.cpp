#include "stdafx.h"
#include "ramp.hpp"
#include "utils/misc.hpp"
#include "implementations.h"
#include "internal/internalObjects.h"

YSE::DSP::ramp::ramp() : impl(new rampImpl) {
  impl->_1overN = 1.0f / getLength();
  impl->_dspTickToMSEC = sampleRate / (1000.0f * getLength());
}

YSE::DSP::ramp::~ramp() {
  delete impl;
}

YSE::DSP::ramp& YSE::DSP::ramp::set(Flt target, Int time) {
	if (time <= 0) {
		impl->target = target;
    impl->current = target;
		impl->ticksLeft = 0;
		impl->reTarget = false;
	} else {
		impl->target = target;
		impl->reTarget = true;
		impl->time = (Flt)time;
	}
	return (*this);
}

YSE::DSP::ramp& YSE::DSP::ramp::setIfNew(Flt target, Int time) {
  if (impl->target != target) set(target, time);
  return *this;
}

YSE::DSP::ramp& YSE::DSP::ramp::stop() {
	impl->target = impl->current.load();
	impl->ticksLeft = 0;
	impl->reTarget = false;
	return (*this);
}

YSE::DSP::ramp& YSE::DSP::ramp::update() {
	if (impl->reTarget) {
		Int nTicks = (Int)(impl->time * impl->_dspTickToMSEC);
		if (!nTicks) nTicks = 1;
		impl->ticksLeft = nTicks;
		impl->bigInc = (impl->target - impl->current) / (Flt)nTicks;
		impl->inc = impl->_1overN * impl->bigInc;
		impl->reTarget = false;
	}

	Int l = getLength();
	Flt * ptr = getBuffer();
	if (impl->ticksLeft) {
		Flt f = impl->current;
		while (l--) *ptr++ = f, f += impl->inc;
    impl->current.store(impl->current.load() +  impl->bigInc);
		impl->ticksLeft--;
	} else {
    Flt f = impl->current = impl->target.load();
		for (; l > 7; l -= 8, ptr += 8) {
			ptr[0] = f; ptr[1] = f; ptr[2] = f; ptr[3] = f;
			ptr[4] = f; ptr[5] = f; ptr[6] = f; ptr[7] = f;
		}
		while (l--) *ptr++ = f;
	}

	return (*this);
}

SAMPLE YSE::DSP::ramp::operator()() {
	return *this;
}

SAMPLE YSE::DSP::ramp::getSample() {
	return *this;
}

Flt YSE::DSP::ramp::getValue() {
	return impl->current;
}

/************************************************************************/


void YSE::DSP::FastFadeIn(sample& s, UInt length) {
	Clamp(length, 1, s.getLength());
	Flt step = 1.0f / (Flt)length;
	Flt multiplier = 0.0f;
	Flt * ptr = s.getBuffer();
	for (UInt i = 0; i < length; i++) {
		*ptr++ *= multiplier;
		multiplier += step;
	}
}

/************************************************************************/

void YSE::DSP::FastFadeOut(sample& s, UInt length) {
	Clamp(length, 1, s.getLength());
	Flt step = 1.0f / (Flt)length;
	Flt multiplier = 1.0f;
	Flt * ptr = s.getBuffer();
	for (UInt i = 0; i < length; i++) {
		*ptr++ *= multiplier;
		multiplier -= step;
	}
	UInt leftOvers = s.getLength() - length;
	while (leftOvers--) *ptr++ = 0.0f;
}

/************************************************************************/

void YSE::DSP::ChangeGain(sample& s, Flt currentGain, Flt newGain, UInt length) {
	if (currentGain == newGain) {
		s *= newGain;
		return;
	}

	Clamp(length, 1, s.getLength());
	Flt step = (newGain - currentGain) / (Flt)length;
	Flt multiplier = currentGain;
	Flt * ptr = s.getBuffer();
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

YSE::DSP::lint::lint() : impl(new lintImpl) {}
YSE::DSP::lint::~lint() { delete impl; }


Flt YSE::DSP::lint::operator()() {
  return impl->current;
}

Flt YSE::DSP::lint::target() {
  return impl->target;
}

YSE::DSP::lint& YSE::DSP::lint::update() {
  if (impl->calculate) {
    impl->current = impl->current + impl->step;
    if (impl->up && impl->current >= impl->target) impl->calculate = false;
    if (!impl->up && impl->current <= impl->target) impl->calculate = false;
  }
  return *this;
}

YSE::DSP::lint& YSE::DSP::lint::stop() {
  impl->target = impl->current.load();
  impl->calculate = false;
  return *this;
}

YSE::DSP::lint& YSE::DSP::lint::setIfNew(Flt target, Int time) {
  if (impl->target != target) set(target, time);
  return *this;
}

YSE::DSP::lint& YSE::DSP::lint::set(Flt target, Int time) {
  if (time == 0) {
    impl->current = impl->target = target;
    impl->calculate = false;
  } else {
    Flt difference = target - impl->current;
    if (difference != 0) {
      impl->step = difference / (Flt)(impl->stepSecond * (time / 1000.0f));
      impl->up = difference > 0;
      impl->calculate = true;
    } else {
      impl->calculate = false;
    }
    impl->target = target;
  }
  return *this;
}

