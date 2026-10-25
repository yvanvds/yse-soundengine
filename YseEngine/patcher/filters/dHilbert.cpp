#include "dHilbert.h"
#include "../pSelector.h"

#include <string_view>

using namespace YSE::PATCHER;

#define className dHilbert

CONSTRUCT_DSP() {
  // in 0: the signal to transform; messages
  ADD_IN_0;
  REG_SIGNAL_IN;

  // out 0-1: real, imaginary
  ADD_OUT_BUFFER;
  ADD_OUT_BUFFER;

  ADD_DESCRIPTION("Phase quadrature filter: splits a signal into two outputs 90 degrees apart "
                  "across the band, the real and imaginary parts of its analytic signal. For "
                  "single-sideband modulation, frequency shifting and envelope following.");
  ADD_CATEGORY(pCategory::FILTER);
  INLET_DOC(0, "in",
            "The signal to transform (unconnected reads 0). 'clear' sets the filter state to 0.",
            "");
  OUTLET_DOC(0, "real", "The real part: the input through an allpass chain, at the same level.",
             "");
  OUTLET_DOC(1, "imaginary",
             "The imaginary part: 90 degrees behind the real part, within 1 degree from 0.0018 "
             "to 0.308 times the sample rate.",
             "");
}

MESSAGES() {
  (void)value;
  // The first word, walked in place so nothing allocates on whichever
  // thread delivers the message.
  const char* chars = message.c_str();
  const std::size_t size = message.size();
  std::size_t begin = 0;
  while (begin < size && IsSelectorSeparator(chars[begin]))
    begin++;
  std::size_t end = begin;
  while (end < size && !IsSelectorSeparator(chars[end]))
    end++;
  if (std::string_view(chars + begin, end - begin) == "clear") clearRequested = true;
}

CALC() {
  if (clearRequested.exchange(false)) fn.clear();

  // Unconnected inlet (#1036): the input reads 0.
  fn(inputs[0].Signal(0.f));
  // Right outlet first, as Max fires outlets right to left.
  outputs[1].SendBuffer(&fn.imaginary(), thread);
  outputs[0].SendBuffer(&fn.real(), thread);
}

#undef className
