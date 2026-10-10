#pragma once
// `.bendout` (issue #532) — the pitch bend message generator.
//
// The one channel-voice message the patcher could not produce. `.noteon`,
// `.noteoff`, `.controlchange`, `.polypressure`, `.channelpressure` and
// `.programchange` cover the rest of the status nibbles; 0xE0 had nothing, so
// a patch that wanted to bend a note had to hand-assemble the three bytes and
// push them into `.midiout` itself.
//
// This is a direct sibling of those six: one hot inlet that both stores the
// value and fires, a `channel` creation argument rather than Max's cold inlet,
// and a raw three-byte string on the outlet — the shape `.midiout` reads and
// the reason the object exists.
//
// **7-bit.** The wire message carries 14 bits as a fine byte then a coarse
// one; this object sends the value as the coarse byte and 0 as the fine one,
// which is Max's `bendout` and the resolution `.bendin`, `.midiparse` and
// `.midiformat` already agree on. The full 14-bit form is `.xbendout` (#533),
// so a patch that needs the fine byte has an object for it rather than this
// one growing a mode.
//
// **No platform guard.** This object opens no device — it only formats bytes
// onto its outlet — so it is compiled and registered on every platform, like
// the six senders it belongs with. Issue #746 lifted the `#if YSE_WINDOWS`
// they all used to carry; only `.midiout`, which holds an RtMidi port, is
// still conditional (on `YSE_ENABLE_MIDI_DEVICE`).
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(mMidiBendOut, YSE::OBJ::M_BENDOUT)
    _NO_MESSAGES
    _DO_CALCULATE

    _INT_IN(SetIntValue)

  private:
    int cvalue;
    int channel;
  };

} // namespace PATCHER
} // namespace YSE
