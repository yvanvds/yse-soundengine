/*
  yse_instrument_internal.hpp — private cross-TU accessors for the instrument
  asset handles (YseSfzInstrument / YseDx7Bank). Not installed; never included
  by a C ABI consumer.

  The handle impl structs and the live-handle registry live in
  yse_instrument.cpp (the only TU that owns their layout and lifetime). These
  accessors let yse_synth.cpp reach the engine payload behind a handle WITHOUT
  duplicating the registry or the impl struct — each accessor validates the
  handle against the registry and returns nullptr (logging the misuse) for a
  NULL or already-destroyed handle, exactly like the public entry points.

  Both engine headers are included here so the accessor signatures name the
  real engine types; keep this out of yse_c_internal.hpp so that shared header
  stays free of engine includes.
*/

#ifndef YSE_C_INSTRUMENT_INTERNAL_HPP
#define YSE_C_INSTRUMENT_INTERNAL_HPP

#include <memory>

#include "include/yse_c/yse_instrument.h"
#include "../synth/samplerVoice.hpp"
#include "../dsp/fm/dx7Sysex.hpp"

namespace yse_c {

  // The shared SFZ instrument behind a live handle, or nullptr (logged) for a
  // NULL / already-destroyed handle. Returned by value so the caller retains a
  // share (setInstrument keeps the PCM alive independently of the handle).
  std::shared_ptr<YSE::SYNTH::samplerInstrument>
  sampler_instrument_from_handle(YseSfzInstrument* h);

  // The parsed DX7 bank behind a live handle, or nullptr (logged) for a NULL /
  // already-destroyed handle. Borrowed — the bank stays owned by the handle;
  // callers copy the patch they select out of it.
  const YSE::SYNTH::dx7Bank* dx7_bank_from_handle(YseDx7Bank* h);

} // namespace yse_c

#endif
