/*
  ==============================================================================

    filters.cpp
    Created: 31 Jan 2014 2:53:30pm
    Author:  yvan

  ==============================================================================
*/

#include "filters.hpp"
#include <cmath>
#include <limits>
#include <math.h>
#include "../headers/constants.hpp"
#include "generators/phaseStep.hpp"
#include "generators/triggerCondition.hpp"
#include "filters/filterCoefficients.hpp"
#include "../utils/misc.hpp"

YSE::DSP::filterBase::filterBase()
  : freq(0),
    gain(0),
    q(0),
    last(0),
    previous(0),
    coef1(0),
    coef2(0),
    ff1(0),
    ff2(0),
    ff3(0),
    fb1(0),
    fb2(0) {}

YSE::DSP::filterBase::filterBase(const YSE::DSP::filterBase& source) {
  freq.store(source.freq);
  gain.store(source.gain);
  q = source.q;
  last = source.last;
  previous = source.previous;
  coef1.store(source.coef1);
  coef2.store(source.coef2);
  ff1.store(source.ff1);
  ff2.store(source.ff2);
  ff3.store(source.ff3);
  fb1.store(source.fb1);
  fb2.store(source.fb2);
}

YSE::DSP::highPass& YSE::DSP::highPass::setFrequency(Flt f) {
  if (f < 0) f = 0;
  freq = f;
  coef1 = 1 - f * (2 * 3.14159f) / SAMPLERATE;
  Clamp(coef1, 0.f, 1.f);

  return (*this);
}

YSE::DSP::buffer& YSE::DSP::highPass::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt* inPtr = in.getPtr();
  Flt* outPtr = samples.getPtr();
  UInt length = in.getLength();
  Flt coef = coef1;
  if (coef < 1) {
    for (UInt i = 0; i < length; i++) {
      Flt f = *inPtr++ + coef * last;
      *outPtr++ = f - last;
      last = f;
    }
  } else {
    last = 0;
    return in;
  }

  return samples;
}

/*******************************************************************************************/

YSE::DSP::lowPass& YSE::DSP::lowPass::setFrequency(Flt f) {
  if (f < 0) f = 0;
  freq = f;
  coef1 = f * (2 * 3.14159f) / SAMPLERATE;
  Clamp(coef1, 0.f, 1.f);

  return (*this);
}

YSE::DSP::buffer& YSE::DSP::lowPass::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt* inPtr = in.getPtr();
  Flt* outPtr = samples.getPtr();
  UInt length = in.getLength();

  Flt feedback = 1 - coef1;
  for (UInt i = 0; i < length; i++) {
    last = *outPtr++ = coef1 * *inPtr++ + feedback * last;
  }

  return samples;
}

/*******************************************************************************************/

YSE::DSP::bandPass& YSE::DSP::bandPass::set(Flt freq, Flt q) {
  this->freq = freq;
  this->q = q;
  calcCoef();
  return (*this);
}

YSE::DSP::bandPass& YSE::DSP::bandPass::setFrequency(Flt freq) {
  this->freq = freq;
  calcCoef();
  return (*this);
}

YSE::DSP::bandPass& YSE::DSP::bandPass::setQ(Flt q) {
  this->q = q;
  calcCoef();
  return (*this);
}

void YSE::DSP::bandPass::calcCoef() {
  Flt r, oneminusr, omega;
  if (freq < 0.001) freq = 10;
  if (q < 0) q = 0;
  omega = freq * (2.0f * 3.14159f) / SAMPLERATE;
  if (q < 0.001)
    oneminusr = 1.0f;
  else
    oneminusr = omega / q;
  if (oneminusr > 1.0f) oneminusr = 1.0f;
  r = 1.0f - oneminusr;
  const Flt c1 = 2.0f * qCos(omega) * r;
  const Flt c2 = -r * r;
  coef1 = c1;
  coef2 = c2;

  // Normalize to unity gain at the centre frequency (#1022): the output is
  // gain / D(e^{j omega}) with D(z) = 1 - c1 z^-1 - c2 z^-2, so gain = |D|.
  // At r = 0 (filter fully open) D = 1, so this is continuous in Q.
  const Flt re = 1.0f - c1 * cosf(omega) - c2 * cosf(2.0f * omega);
  const Flt im = c1 * sinf(omega) + c2 * sinf(2.0f * omega);
  gain = sqrtf(re * re + im * im);
}

float YSE::DSP::bandPass::qCos(Flt omega) {
  if (omega >= -(0.5 * 3.14159f) && omega <= 0.5f * 3.14159f) {
    Flt result = omega * omega;
    return (((result * result * result * (-1.0f / 720.0f) + result * result * (1.0f / 24.0f)) -
             result * 0.5f) +
            1);
  } else
    return 0;
}

YSE::DSP::bandPass::bandPass() {
  calcCoef();
}

YSE::DSP::buffer& YSE::DSP::bandPass::operator()(YSE::DSP::buffer& in) {
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt* inPtr = in.getPtr();
  Flt* outPtr = samples.getPtr();
  UInt length = in.getLength();

  // copy to Flt because we don't want atomic in whole buffer loops
  Flt cf1 = coef1;
  Flt cf2 = coef2;

  for (UInt i = 0; i < length; i++) {
    Flt output = *inPtr++ + cf1 * last + cf2 * previous;
    *outPtr++ = gain * output;
    previous = last;
    last = output;
  }

  return samples;
}

/*******************************************************************************************/

YSE::DSP::biQuad::biQuad() : type(BQ_LOWPASS) {}

YSE::DSP::biQuad::biQuad(const YSE::DSP::biQuad& source)
  : filterBase(source),
    type(source.type),
    direct(source.direct.load()),
    ramp(source.ramp),
    smoothing(source.smoothing),
    started(source.started),
    x1(source.x1),
    x2(source.x2),
    y1(source.y1),
    y2(source.y2) {
  for (int i = 0; i < 5; ++i) {
    current[i] = source.current[i];
    target[i] = source.target[i];
    step[i] = source.step[i];
  }
}

YSE::DSP::biQuad& YSE::DSP::biQuad::coefficients(Flt a0, Flt a1, Flt a2, Flt b1, Flt b2) {
  direct = true;
  const Flt values[5] = {a0, a1, a2, b1, b2};
  bool changed = false;
  for (int i = 0; i < 5; ++i) {
    if (!std::isfinite(values[i])) continue;
    const Dbl v = static_cast<Dbl>(values[i]);
    if (v != target[i]) {
      target[i] = v;
      changed = true;
    }
  }
  if (!changed) return (*this);
  if (smoothing && started) {
    for (int i = 0; i < 5; ++i)
      step[i] = (target[i] - current[i]) / static_cast<Dbl>(SMOOTH_SAMPLES);
    ramp = SMOOTH_SAMPLES;
  } else {
    for (int i = 0; i < 5; ++i) {
      current[i] = target[i];
      step[i] = 0.0;
    }
    ramp = 0;
  }
  return (*this);
}

YSE::DSP::biQuad::coefficientSet YSE::DSP::biQuad::coefficients() const {
  coefficientSet out;
  out.a0 = static_cast<Flt>(target[0]);
  out.a1 = static_cast<Flt>(target[1]);
  out.a2 = static_cast<Flt>(target[2]);
  out.b1 = static_cast<Flt>(target[3]);
  out.b2 = static_cast<Flt>(target[4]);
  return out;
}

YSE::DSP::biQuad& YSE::DSP::biQuad::smooth(Bool on) {
  smoothing = on;
  // Turning it off ends a move under way.
  if (!on && ramp > 0) {
    for (int i = 0; i < 5; ++i) {
      current[i] = target[i];
      step[i] = 0.0;
    }
    ramp = 0;
  }
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::clear() {
  last = 0;
  previous = 0;
  x1 = x2 = y1 = y2 = 0.0;
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::stoke(Flt y1Value, Flt y2Value, Flt x1Value, Flt x2Value) {
  const auto finite = [](Flt v) { return std::isfinite(v) ? static_cast<Dbl>(v) : 0.0; };
  y1 = finite(y1Value);
  y2 = finite(y2Value);
  x1 = finite(x1Value);
  x2 = finite(x2Value);
  return (*this);
}

void YSE::DSP::biQuad::Advance() {
  if (ramp == 0) return;
  if (--ramp == 0) {
    for (int i = 0; i < 5; ++i) {
      current[i] = target[i];
      step[i] = 0.0;
    }
  } else {
    for (int i = 0; i < 5; ++i)
      current[i] += step[i];
  }
}

Flt YSE::DSP::biQuad::Step(Flt in) {
  started = true;
  if (!std::isfinite(in)) return static_cast<Flt>(y1);
  const Dbl x = static_cast<Dbl>(in);
  const Dbl y =
      current[0] * x + current[1] * x1 + current[2] * x2 - current[3] * y1 - current[4] * y2;
  // Blown up: start again from silence rather than latch infinity.
  if (!std::isfinite(y) || std::abs(y) > static_cast<Dbl>(std::numeric_limits<Flt>::max())) {
    x1 = x2 = y1 = y2 = 0.0;
    return 0.f;
  }
  x2 = x1;
  x1 = x;
  y2 = y1;
  y1 = y;
  return static_cast<Flt>(y);
}

Flt YSE::DSP::biQuad::process(Flt in) {
  direct = true;
  Advance();
  return Step(in);
}

YSE::DSP::buffer& YSE::DSP::biQuad::operator()(YSE::DSP::buffer& in, YSE::DSP::buffer* a0,
                                               YSE::DSP::buffer* a1, YSE::DSP::buffer* a2,
                                               YSE::DSP::buffer* b1, YSE::DSP::buffer* b2) {
  direct = true;
  UInt length = in.getLength() > STANDARD_BUFFERSIZE ? STANDARD_BUFFERSIZE : in.getLength();
  YSE::DSP::buffer* signals[5] = {a0, a1, a2, b1, b2};
  for (auto* s : signals)
    if (s != nullptr && s->getLength() < length) length = s->getLength();
  // Within the capacity set at construction: no allocation.
  if (samples.getLength() != length) samples.resize(length);

  const Flt* x = in.getPtr();
  Flt* y = samples.getPtr();
  for (UInt i = 0; i < length; ++i) {
    Advance();
    for (int c = 0; c < 5; ++c) {
      if (signals[c] == nullptr) continue;
      const Flt v = signals[c]->getPtr()[i];
      // A signal coefficient becomes the value a later scalar change moves
      // from; a non-finite sample keeps the one before.
      if (std::isfinite(v)) current[c] = target[c] = static_cast<Dbl>(v);
      step[c] = 0.0;
    }
    y[i] = Step(x[i]);
  }
  return samples;
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setType(BQ_TYPE type) {
  direct = false;
  this->type = type;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setFreq(Flt frequency) {
  direct = false;
  freq = frequency;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setQ(Flt Q) {
  direct = false;
  q = Q;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setPeak(Flt peakGain) {
  direct = false;
  gain = peakGain;
  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::set(BQ_TYPE type, Flt frequency, Flt Q, Flt peakGain) {
  direct = false;
  this->type = type;
  freq = frequency;
  q = Q;
  gain = peakGain;

  calc();
  return (*this);
}

YSE::DSP::biQuad& YSE::DSP::biQuad::setRaw(Flt fb1, Flt fb2, Flt ff1, Flt ff2, Flt ff3) {
  direct = false;
  Flt discriminant = fb1 * fb1 + 4 * fb2;
  Bool zero = true;
  if (discriminant < 0) {
    if (fb2 >= -1.0f) zero = false;
  } else {
    if (fb1 <= 2.0f && fb1 >= -2.0f && 1.0f - fb1 - fb2 >= 0 && 1.0f + fb1 - fb2 >= 0) zero = false;
  }
  if (zero) fb1 = fb2 = ff1 = ff2 = ff3 = 0;
  this->fb1 = fb1;
  this->fb2 = fb2;
  this->ff1 = ff1;
  this->ff2 = ff2;
  this->ff3 = ff3;

  return (*this);
}

YSE::DSP::buffer& YSE::DSP::biQuad::operator()(YSE::DSP::buffer& in) {
  if (direct) return (*this)(in, nullptr, nullptr, nullptr, nullptr, nullptr);
  if (in.getLength() != samples.getLength()) samples.resize(in.getLength());

  Flt* inPtr = in.getPtr();
  Flt* outPtr = samples.getPtr();
  UInt length = in.getLength();

  Flt output;

  // copy to float because we don't want atomic to be used in a whole buffer calculation
  Flt b1 = fb1;
  Flt b2 = fb2;
  Flt f1 = ff1;
  Flt f2 = ff2;
  Flt f3 = ff3;

  for (UInt i = 0; i < length; i++) {
    output = *inPtr++ + b1 * last + b2 * previous;
    *outPtr++ = f1 * output + f2 * last + f3 * previous;
    previous = last;
    last = output;
  }

  return samples;
}

void YSE::DSP::biQuad::calc() {
  Flt norm;
  Flt v = (float)pow(10, std::abs(gain.load()) / 20.0f);
  Flt k = tan(Pi * freq / static_cast<Flt>(SAMPLERATE));

  // The lowpass, highpass, bandpass and notch are filterCoefficients'
  // (#1092), whose cookbook formulas are these filters exactly: shared rather
  // than written twice. The peak and the shelves below are different designs.
  const auto shared = [this](filterCoefficients::TYPE design) {
    const filterCoefficients::coefficientSet c =
        filterCoefficients::Compute(design, freq, 1.f, q, static_cast<Flt>(SAMPLERATE));
    ff1 = c.a0;
    ff2 = c.a1;
    ff3 = c.a2;
    fb1 = c.b1;
    fb2 = c.b2;
  };

  switch (type) {
  case BQ_LOWPASS: {
    shared(filterCoefficients::LOWPASS);
    break;
  }
  case BQ_HIGHPASS: {
    shared(filterCoefficients::HIGHPASS);
    break;
  }
  case BQ_BANDPASS: {
    shared(filterCoefficients::BANDPASS);
    break;
  }
  case BQ_NOTCH: {
    shared(filterCoefficients::BANDSTOP);
    break;
  }
  case BQ_PEAK: {
    if (gain >= 0) {
      norm = 1 / (1 + 1 / q * k + k * k);
      ff1 = (1 + v / q * k + k * k) * norm;
      ff2 = 2 * (k * k - 1) * norm;
      ff3 = (1 - v / q * k + k * k) * norm;
      fb1 = ff2.load();
      fb2 = (1 - 1 / q * k + k * k) * norm;
    } else {
      norm = 1 / (1 + v / q * k + k * k);
      ff1 = (1 + 1 / q * k + k * k) * norm;
      ff2 = 2 * (k * k - 1) * norm;
      ff3 = (1 - 1 / q * k + k * k) * norm;
      fb1 = ff2.load();
      fb2 = (1 - v / q * k + k * k) * norm;
    }
    break;
  }
  case BQ_LOWSHELF: {
    if (gain >= 0) {
      norm = 1 / (1 + Sqrt2 * k + k * k);
      ff1 = (1 + sqrt(2 * v) * k + v * k * k) * norm;
      ff2 = 2 * (v * k * k - 1) * norm;
      ff3 = (1 - sqrt(2 * v) * k + v * k * k) * norm;
      fb1 = 2 * (k * k - 1) * norm;
      fb2 = (1 - Sqrt2 * k + k * k) * norm;
    } else {
      norm = 1 / (1 + sqrt(2 * v) * k + v * k * k);
      ff1 = (1 + Sqrt2 * k + k * k) * norm;
      ff2 = 2 * (k * k - 1) * norm;
      ff3 = (1 - Sqrt2 * k + k * k) * norm;
      fb1 = 2 * (v * k * k - 1) * norm;
      fb2 = (1 - sqrt(2 * v) * k + v * k * k) * norm;
    }
    break;
  }
  case BQ_HIGHSHELF: {
    if (gain >= 0) {
      norm = 1 / (1 + Sqrt2 * k + k * k);
      ff1 = (v + sqrt(2 * v) * k + k * k) * norm;
      ff2 = 2 * (k * k - v) * norm;
      ff3 = (v - sqrt(2 * v) * k + k * k) * norm;
      fb1 = 2 * (k * k - 1) * norm;
      fb2 = (1 - Sqrt2 * k + k * k) * norm;
    } else {
      norm = 1 / (v + sqrt(2 * v) * k + k * k);
      ff1 = (1 + Sqrt2 * k + k * k) * norm;
      ff2 = 2 * (k * k - 1) * norm;
      ff3 = (1 - Sqrt2 * k + k * k) * norm;
      fb1 = 2 * (k * k - v) * norm;
      fb2 = (v - sqrt(2 * v) * k + k * k) * norm;
    }
    break;
  }
  }
  fb1 = -fb1;
  fb2 = -fb2;

  Flt discriminant = fb1 * fb1 + 4 * fb2;
  Bool zero = true;
  if (discriminant < 0) {
    if (fb2 >= -1.0f) zero = false;
  } else {
    if (fb1 <= 2.0f && fb1 >= -2.0f && 1.0f - fb1 - fb2 >= 0 && 1.0f + fb1 - fb2 >= 0) zero = false;
  }
  if (zero) fb1 = fb2 = ff1 = ff2 = ff3 = 0;
}

/*******************************************************************************************/

YSE::DSP::sampleHold::sampleHold()
  : lastIn(0), lastOut(0), thresh(0), mode(DECREASE), length(0), samples(STANDARD_BUFFERSIZE) {}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::reset(Flt value) {
  lastIn = value;
  return (*this);
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::set(Flt value) {
  lastOut = value;
  return (*this);
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::threshold(Flt value) {
  thresh = value;
  return (*this);
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::triggerMode(TRIGGER_MODE m) {
  mode = m;
  return (*this);
}

YSE::DSP::sampleHold& YSE::DSP::sampleHold::duration(UInt samplesToHold) {
  length = samplesToHold;
  return (*this);
}

// The trigger modes are numbered as triggerCondition::Meets reads them
// (shared with signalStash, #1084); anything past PHASE is a decrease.
static_assert(YSE::DSP::sampleHold::ASCENDING == 0 && YSE::DSP::sampleHold::DESCENDING == 1 &&
                  YSE::DSP::sampleHold::BOTH == 2 && YSE::DSP::sampleHold::EQUALS == 3 &&
                  YSE::DSP::sampleHold::ZERO_TO_NONZERO == 4 &&
                  YSE::DSP::sampleHold::NONZERO_TO_ZERO == 5 && YSE::DSP::sampleHold::PHASE == 6 &&
                  YSE::DSP::sampleHold::DECREASE == 7,
              "sampleHold's trigger modes match triggerCondition::Meets");

namespace {
  // Whether the control step from `previous` to `current` meets `mode`.
  inline bool Triggers(int mode, Flt previous, Flt current, Flt threshold, Dbl wrappedThreshold) {
    return YSE::DSP::triggerCondition::Meets(mode, previous, current, threshold, wrappedThreshold);
  }
} // namespace

YSE::DSP::buffer& YSE::DSP::sampleHold::operator()(YSE::DSP::buffer& in, YSE::DSP::buffer& signal) {
  UInt n = in.getLength() < signal.getLength() ? in.getLength() : signal.getLength();
  if (n > STANDARD_BUFFERSIZE) n = STANDARD_BUFFERSIZE;
  // Within the capacity set in the constructor: no allocation.
  if (samples.getLength() != n) samples.resize(n);

  const Flt* inPtr = in.getPtr();
  const Flt* sigPtr = signal.getPtr();
  Flt* outPtr = samples.getPtr();

  // it's a bad idea to parse the whole buffer with atomics, so make a copy first
  Flt li = lastIn;
  Flt lo = lastOut;
  const Flt th = thresh;
  const int m = mode;
  const UInt hold = length;
  const Dbl wrapped = phaseStep::Wrap(static_cast<Dbl>(th));

  for (UInt i = 0; i < n; i++) {
    const Flt control = sigPtr[i];
    if (std::isfinite(control)) {
      if (Triggers(m, li, control, th, wrapped)) {
        lo = inPtr[i];
        remaining = hold;
      }
      li = control;
    }
    if (hold == 0) {
      outPtr[i] = lo;
    } else if (remaining > 0) {
      outPtr[i] = lo;
      remaining--;
    } else {
      outPtr[i] = 0.f;
    }
  }
  lastIn = li;
  lastOut = lo;

  return samples;
}
