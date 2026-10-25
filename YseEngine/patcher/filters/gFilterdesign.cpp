#include "gFilterdesign.h"
#include "../math/gExprEval.h"
#include "../pListArgs.h"
#include "../pObjectList.hpp"
#include "../pSelector.h"

#include <cmath>
#include <complex>
#include <string_view>

using namespace YSE::PATCHER;

namespace {
  // The longest list the object sends: the transfer function's or the
  // zeroes', at most 2 * MAX_ORDER numbers after a word, or the sections',
  // five numbers each. A number takes at most kExprValueTextMax characters
  // with its separator.
  constexpr std::size_t SCRATCH_CAPACITY =
      (YSE::DSP::filterDesign::MAX_SECTIONS * 5 + 2 * YSE::DSP::filterDesign::MAX_ORDER + 2) *
      (YSE::PATCHER::kExprValueTextMax + 1);

  // The token at or after `from`, walked in place so nothing allocates on
  // whichever thread delivers the message. `next` is where it ends; empty
  // when there is none.
  std::string_view Token(const std::string& text, std::size_t from, std::size_t& next) {
    const char* chars = text.c_str();
    const std::size_t size = text.size();
    std::size_t begin = from;
    while (begin < size && IsSelectorSeparator(chars[begin]))
      begin++;
    std::size_t end = begin;
    while (end < size && !IsSelectorSeparator(chars[end]))
      end++;
    next = end;
    return std::string_view(chars + begin, end - begin);
  }

  bool ReadNumber(std::string_view token, float& out) {
    return !token.empty() && ReadNumericToken(token.data(), token.size(), out) &&
           std::isfinite(out);
  }

  void AppendNumber(std::string& out, double value) {
    char text[kExprValueTextMax];
    const int written =
        ExprFormatValue(ExprValue::Float(static_cast<float>(value)), text, kExprValueTextMax);
    if (written <= 0) return;
    if (!out.empty()) out.push_back(' ');
    out.append(text, static_cast<std::size_t>(written));
  }

  using YSE::DSP::filterDesign;
} // namespace

#define className gFilterdesign

CONSTRUCT() {
  ADD_IN_0;
  REG_BANG_IN(Bang);
  REG_LIST_IN(List);

  // out 0: the sections; out 1: zeroes, poles, gain and transfer function
  ADD_OUT_LIST;
  ADD_OUT_LIST;

  ADD_PARAM(settings);
  REG_PARM_CLEAR;
  REG_PARM_PARSE;

  scratch.reserve(SCRATCH_CAPACITY);

  ADD_DESCRIPTION("Designs a Butterworth or Chebyshev lowpass, highpass, bandpass or bandstop "
                  "filter of a given order and sends it as second-order sections, a coefficient "
                  "list ~cascade and .filterdetail take as they are.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "Bang: design the filter and send it. A list of settings, each a word and its value, "
            "sets them without sending: response (lowpass, highpass, bandpass, bandstop), "
            "topology (butterworth, chebyshev-1, chebyshev-2), order, frequency (one, or two band "
            "edges), units (hertz, normalized, radians), samplerate (0: the engine's), "
            "passband_ripple and stopband_attenuation (dB), sos_output, zpk_output and tf_output "
            "(0 or 1).",
            "");
  OUTLET_DOC(0, "sections",
             "The second-order sections, five coefficients a0 a1 a2 b1 b2 per section, the overall "
             "gain in the first; sent when sos_output is on (the default).",
             "a list of coefficients in sets of five");
  OUTLET_DOC(1, "details",
             "With zpk_output on: 'zeros' and 'poles' followed by real and imaginary parts, and "
             "'gain'. With tf_output on: 'numerator' and 'denominator', the coefficients of z^0, "
             "z^-1, ... Sent before the sections.",
             "");
  PARAM_DOC("settings", "",
            "Starting settings in the words of the inlet ('.filterdesign response bandpass order "
            "4 frequency 500 2000'). The defaults: a Butterworth lowpass of order 2 at 1000 Hz "
            "(band edges 1000 and 2000), 1 dB ripple, 40 dB attenuation, sections only. Later "
            "messages do not change what is saved.",
            "words and values");
}

void gFilterdesign::ResetSettings() {
  fn = filterDesign();
  sosOutput = true;
  zpkOutput = false;
  tfOutput = false;
}

void gFilterdesign::Apply(const std::string& text) {
  std::size_t next = 0;
  std::string_view word = Token(text, 0, next);
  while (!word.empty()) {
    std::size_t after = next;
    const std::string_view value = Token(text, after, after);
    float number = 0.f;
    bool used = true;
    if (word == "response") {
      if (value == "lowpass")
        fn.response(filterDesign::RESPONSE::LOWPASS);
      else if (value == "highpass")
        fn.response(filterDesign::RESPONSE::HIGHPASS);
      else if (value == "bandpass")
        fn.response(filterDesign::RESPONSE::BANDPASS);
      else if (value == "bandstop")
        fn.response(filterDesign::RESPONSE::BANDSTOP);
      else
        used = false;
    } else if (word == "topology") {
      if (value == "butterworth")
        fn.topology(filterDesign::TOPOLOGY::BUTTERWORTH);
      else if (value == "chebyshev-1")
        fn.topology(filterDesign::TOPOLOGY::CHEBYSHEV_1);
      else if (value == "chebyshev-2")
        fn.topology(filterDesign::TOPOLOGY::CHEBYSHEV_2);
      else
        used = false;
    } else if (word == "units") {
      if (value == "hertz")
        fn.units(filterDesign::UNITS::HERTZ);
      else if (value == "normalized")
        fn.units(filterDesign::UNITS::NORMALIZED);
      else if (value == "radians")
        fn.units(filterDesign::UNITS::RADIANS);
      else
        used = false;
    } else if (word == "frequency") {
      if (ReadNumber(value, number)) {
        // An optional second number: the upper band edge.
        std::size_t further = after;
        float second = 0.f;
        if (ReadNumber(Token(text, after, further), second)) {
          fn.frequency(number, second);
          after = further;
        } else {
          fn.frequency(number);
        }
      } else {
        used = false;
      }
    } else if (ReadNumber(value, number)) {
      if (word == "order")
        fn.order(number < 1.f ? 1u : static_cast<unsigned int>(std::lround(number)));
      else if (word == "samplerate")
        fn.sampleRate(number);
      else if (word == "passband_ripple")
        fn.passbandRipple(number);
      else if (word == "stopband_attenuation")
        fn.stopbandAttenuation(number);
      else if (word == "sos_output")
        sosOutput = number != 0.f;
      else if (word == "zpk_output")
        zpkOutput = number != 0.f;
      else if (word == "tf_output")
        tfOutput = number != 0.f;
      else
        used = false;
    } else {
      used = false;
    }
    // A setting takes its value with it; anything else is skipped one token
    // at a time, so the next word is still found.
    if (used) next = after;
    word = Token(text, next, next);
  }
}

// Control thread: a list parameter rebuilds the object, so these run on one
// no other thread has seen.
PARM_CLEAR() {
  settings.clear();
  ResetSettings();
}

PARM_PARSE() {
  std::string text;
  for (const std::string& token : settings) {
    text += token;
    text += ' ';
  }
  ResetSettings();
  Apply(text);
}

LIST_IN(List) {
  // The word bang as text (through a receive) is a bang.
  std::size_t next = 0;
  if (Token(value, 0, next) == "bang" && Token(value, next, next).empty()) {
    Bang(inlet, thread);
    return;
  }
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  Apply(value);
  busy.store(false, std::memory_order_release);
}

BANG_IN(Bang) {
  (void)inlet;
  // Held across the sends: the lists are written in `scratch`, which a
  // second message must not overwrite while a receiver reads it.
  if (busy.exchange(true, std::memory_order_acquire)) {
    dropped.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  if (fn.design()) {
    const unsigned int poles = fn.poles();
    if (zpkOutput) {
      scratch.assign("zeros");
      for (unsigned int i = 0; i < poles; ++i) {
        AppendNumber(scratch, fn.zero(i).real());
        AppendNumber(scratch, fn.zero(i).imag());
      }
      outputs[1].SendList(scratch, thread);
      scratch.assign("poles");
      for (unsigned int i = 0; i < poles; ++i) {
        AppendNumber(scratch, fn.pole(i).real());
        AppendNumber(scratch, fn.pole(i).imag());
      }
      outputs[1].SendList(scratch, thread);
      scratch.assign("gain");
      AppendNumber(scratch, fn.gain());
      outputs[1].SendList(scratch, thread);
    }
    if (tfOutput) {
      scratch.assign("numerator");
      for (unsigned int i = 0; i <= poles; ++i)
        AppendNumber(scratch, fn.numerator(i));
      outputs[1].SendList(scratch, thread);
      scratch.assign("denominator");
      for (unsigned int i = 0; i <= poles; ++i)
        AppendNumber(scratch, fn.denominator(i));
      outputs[1].SendList(scratch, thread);
    }
    if (sosOutput) {
      scratch.clear();
      for (unsigned int s = 0; s < fn.sections(); ++s) {
        const YSE::DSP::biQuad::coefficientSet c = fn.section(s);
        AppendNumber(scratch, c.a0);
        AppendNumber(scratch, c.a1);
        AppendNumber(scratch, c.a2);
        AppendNumber(scratch, c.b1);
        AppendNumber(scratch, c.b2);
      }
      outputs[0].SendList(scratch, thread);
    }
  }
  busy.store(false, std::memory_order_release);
}

#undef className
