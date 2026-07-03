#pragma once
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gSend, YSE::OBJ::G_SEND)
      _NO_MESSAGES
      _NO_CALCULATE

      _INT_IN(SetIntValue)
      _FLOAT_IN(SetFloatValue)
      _BANG_IN(SetBangValue)
      _LIST_IN(SetListValue)

      // Cache the bus address ("<patcherName>.<dataName>") the moment the
      // parent is known, so the audio-path publishes never concatenate a
      // std::string on the callback thread (issue #187).
      void SetParent(pObject * parent) override;

      // Recompute the cached address after a patcher rename. Mirrors
      // gReceive::Resubscribe(); called from patcherImplementation::SetName so
      // sends keep matching the re-anchored receivers.
      void RefreshBusAddress();

    private:
      // 0 = also fire the in-patcher PassData/PassBang path (default),
      // 1 = publish only to the global bus. Parsed from the second
      // constructor argument (issue #122).
      int globalOnly = 0;

      // Precomputed "<patcherName>.<dataName>" bus address. Written on the main
      // thread (SetParent / RefreshBusAddress), read on the audio thread under
      // the owning patcher's lock; empty until a parent is assigned.
      std::string busAddress_;
  };
}
}