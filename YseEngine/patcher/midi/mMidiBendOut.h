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
// **Platform guard.** `#if YSE_WINDOWS`, matching the six senders it belongs
// with. The guard is wrong for all seven — none of them opens a device, they
// only emit bytes — but lifting it is issue #746's sweep over the whole
// family, not a deviation to introduce here for one object.
#include "headers/defines.hpp"

#if YSE_WINDOWS

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

#endif // YSE_WINDOWS
