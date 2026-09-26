#pragma once
#include "../pObject.h"
#include "../../internal/namedBus.h"

/*
  Named-receive endpoint inside a patcher.

  Forwards values that arrive through the in-patcher `PassData` path *and*
  through the global named bus (issue #122). The bus subscription is keyed
  on "patcher.<patcherName>.<dataName>"; matching producers in *any* patcher with
  the same name reach this receiver. Subscription happens when the
  receiver learns its parent patcher (`SetParent`); destruction
  unsubscribes.
*/

namespace YSE {
  namespace PATCHER {

    PATCHER_CLASS(gReceive, YSE::OBJ::G_RECEIVE)
    _NO_MESSAGES
    _NO_CALCULATE

    _INT_IN(SetIntValue)
    _FLOAT_IN(SetFloatValue)
    _BANG_IN(SetBangValue)
    _LIST_IN(SetListValue)

    // Refuses — logs and clears — a dataName longer than
    // patcherImplementation::MAX_SLOT_NAME_LENGTH, gSend's rule, so a .r keeps
    // subscribing to the address its .s publishes whole (issue #922).
    _PARM_PARSE

  public:
    ~gReceive() override;

    // Override of pObject::SetParent — once the parent patcher is known
    // the receiver attaches itself to the global bus.
    void SetParent(pObject* parent) override;

    // Re-subscribe under the current parent patcher's name. Called by
    // `patcherImplementation::SetName` when the patcher is renamed so
    // existing receivers keep working under the new bus prefix.
    void Resubscribe();
    // The rename hook (issue #893): a patcher rename re-anchors this object.
    void OnPatcherRenamed() override {
      Resubscribe();
    }

  private:
    void unsubscribeIfNeeded();
    void subscribeFromParent();

    YSE::INTERNAL::SubHandle busHandle = 0; // 0 == not subscribed
    // `globalOnly` mirrors the gSend flag but on the receive side it has
    // no behavioural effect today — the bus subscription is unconditional.
    // The parameter slot is reserved so the symmetric DSL surface stays
    // stable when later iterations of issue #119 add receive-side filters.
    int globalOnly = 0;
  };
}
}
