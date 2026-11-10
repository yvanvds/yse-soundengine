#pragma once

#include <string>
#include "../pObject.h"
#include "../../dsp/routing/signalBus.hpp"
#include "signalBusPort.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~receive``: the sum of every ``~send`` of the same name,
     *         without a cord, through a ``DSP::signalBusReceiver`` (issue
     *         #1136).
     *
     *  Ported from Max's ``receive~``. The wrapper only maps the inlet,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: ``set <name>`` receives another name from a later block
     *    (none receives silence).
     *  - Outlet 0: the sum of the name's senders.
     *  - Parameter: ``name``, scoped as for ``~send``. A ``set`` is not
     *    saved.
     *
     *  When it hears a block: a ``~send`` in this patcher is heard in the
     *  same block, because the graph runs it first, unless that order would
     *  close a signal loop; then, and for a ``~send`` in another patcher, the
     *  block before.
     */
    PATCHER_CLASS(dReceive, YSE::OBJ::D_RECEIVE)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _LIST_IN(SetList)

    // Binds the receiver once the patcher (and with it the scope) is known.
    void SetParent(pObject* parent) override;
    // A rename moves the scope: bind to the same name in the new one.
    void OnPatcherRenamed() override;

    /** @brief The name received now. Control thread, under the patcher's
     *         lock. */
    const std::string& BusName() const {
      return port.Name();
    }

    /** @brief Takes a waiting ``set``, rebinding when the name changed.
     *         True when it did. The patcher's signal-bus sync only. */
    bool ApplySet();

    /** @brief Leaves the bus, for good: the object has left the patch.
     *         Control thread, under the patcher's lock. */
    void Leave() {
      receiver.bind(nullptr);
    }

    /** @brief The DSP endpoint. */
    DSP::signalBusReceiver& Receiver() {
      return receiver;
    }

  private:
    // Bind the receiver to the bus for the name in use. Control thread.
    void Rebind();

    DSP::signalBusReceiver receiver;
    signalBusPort port;
    std::string name;
  };

} // namespace PATCHER
} // namespace YSE
