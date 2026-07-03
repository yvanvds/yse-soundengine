#pragma once

namespace YSE {
  namespace PATCHER {

    // Documentation category for a patcher object. The UNSET value is the
    // default; the test_doc_coverage doctest requires every registered object
    // to set this to a real category via ADD_CATEGORY() in its constructor.
    enum class pCategory {
      UNSET = 0,
      OSC, // signal generators (sine, saw, noise, ...)
      FILTER, // filters (lowpass, bandpass, highpass, vcf, ...)
      MATH, // arithmetic / conversion (+, -, *, /, mtof, ...)
      GENERIC, // routing / glue (line, send, receive, switch, ...)
      GUI, // user-facing controls (slider, button, ...)
      TIME, // timing utilities (metro, ...)
      MIDI, // MIDI generation / output
    };

    // Bitmask of message types an inlet currently accepts. Returned by
    // inlet::GetAcceptedTypes(); useful for binding generators that need to
    // know which messages a port consumes without inspecting source code.
    enum InletType : unsigned int {
      IT_NONE = 0,
      IT_BUFFER = 1u << 0,
      IT_FLOAT = 1u << 1,
      IT_INT = 1u << 2,
      IT_BANG = 1u << 3,
      IT_LIST = 1u << 4,
    };

  } // namespace PATCHER
} // namespace YSE
