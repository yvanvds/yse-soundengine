#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~mute``: switches the signal processing of a subpatcher off and
     *         on (issue #1137).
     *
     *  Ported from Max's ``mute~``. A graph feature, not signal processing, so
     *  there is no ``YSE::DSP`` class behind it: the two-layer rule (#1035) is
     *  waived. The object holds no state of its own either; the mute lives on
     *  the subpatcher (gSubpatcher::SetMute).
     *
     *  **Wiring.** Connect the outlet to any inlet of the subpatchers to
     *  control, as many as needed. The cord is resolved like any cord to a
     *  subpatcher, so it ends on the boundary object (``.inlet`` / ``~inlet``)
     *  behind that pin, and GraphState::inletSubpatcher says which subpatcher
     *  that boundary object belongs to. Nothing is ever delivered down the
     *  cord: the boundary object does not see the mute, and neither does
     *  anything connected after it. A cord to anything that is not a
     *  subpatcher's inlet does nothing.
     *
     *  **Messages** (inlet 0):
     *
     *  - int / float: non-zero mutes the signal objects directly inside the
     *    subpatcher (gSubpatcher::MUTE_CONTENTS), 0 unmutes them.
     *  - list ``<mute> <nested>``: the same, and with ``<nested>`` non-zero the
     *    signal objects in its nested subpatchers too, at any depth
     *    (``MUTE_NESTED``): ``1 1`` mutes everything inside, ``0 1``
     *    unmutes everything inside. A one-number list is the int.
     *
     *  A muted object does not compute, and its signal outlets carry silence,
     *  so a muted subpatcher outputs silence rather than its last block.
     *  Messages, polls and control objects inside keep running.
     *
     *  **Saving.** Nothing: the mute is runtime state, as the patcher's DSP
     *  switch is. A loaded patch starts unmuted.
     *
     *  **Real time.** A message parses at most two numbers in place and does
     *  one atomic or/and per cord, against the snapshot the calling thread
     *  pins (outlet::SendSubpatchMute), on any thread: no allocation, no lock.
     *  ``Calculate()`` does nothing.
     */
    PATCHER_CLASS(pMute, YSE::OBJ::D_MUTE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SetInt)
    _FLOAT_IN(SetFloat)
    _LIST_IN(SetList)

  private:
    void Apply(bool mute, bool nested);
  };

} // namespace PATCHER
} // namespace YSE
