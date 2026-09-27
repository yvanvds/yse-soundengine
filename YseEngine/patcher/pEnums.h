#pragma once

namespace YSE {
  namespace PATCHER {

    // Documentation category for a patcher object. The UNSET value is the
    // default; the test_doc_coverage doctest requires every registered object
    // to set this to a real category via ADD_CATEGORY() in its constructor.
    //
    // The category drives the per-category pages of the Sphinx object
    // reference and the palettes host-side editors build from the metadata
    // API, so values are only ever appended (they are mirrored by
    // YsePCategory in the C API). GENERIC is the fallback for an object that
    // fits none of the others; nothing registered uses it today (issue #870).
    enum class pCategory {
      UNSET = 0,
      OSC, // signal generators (sine, saw, noise, ~line, ...)
      FILTER, // filters (lowpass, bandpass, highpass, vcf, ...)
      MATH, // arithmetic / conversion / analysis (+, -, *, /, mtof, ...)
      GENERIC, // uncategorisable fallback
      GUI, // user-facing controls (slider, button, message box, ...)
      TIME, // timing utilities (metro, delay, line, ...)
      MIDI, // MIDI generation, output and device input
      ROUTING, // steering messages between outlets (gate, route, sel, ...)
      CONTROL, // control flow and ordering (trigger, uzi, if, loadbang, ...)
      LIST, // building and taking apart lists (pack, unpack, zl, ...)
      STRING, // symbols and text (sprintf, regexp, tosymbol, ...)
      COLLECTION, // stores (coll, table, bag, funbuff, ...)
      DICT, // the .dict family
      ARRAY, // the .array family
      RANDOM, // random and probabilistic sources (random, drunk, urn, ...)
      IO, // audio and host I/O (~adc, ~dac, send/receive, print, value)
      ENCAPSULATION, // subpatchers and their boundaries (patcher, inlet, outlet)
      SEQUENCE, // timed recorders / players (seq, mtr, qlist)
      COUNT_, // sentinel: number of categories, not a valid value; keep last (C API drift guard)
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
