#pragma once

#include <string>
#include "../pObject.h"
#include "../../dsp/routing/signalBus.hpp"
#include "signalBusPort.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief ``~send``: sends a signal to every ``~receive`` of the same
     *         name, without a cord, through a ``DSP::signalBusSender``
     *         (issue #1136).
     *
     *  Ported from Max's ``send~``. The wrapper only maps the inlet,
     *  messages and parameter onto the DSP class.
     *
     *  - Inlet 0: the signal to send. Signal cords sum (#1036); unconnected,
     *    the last number sent here is sent as a constant. ``set <name>``
     *    sends to another name from a later block (none sends nowhere);
     *    ``clear`` sends silence for one block, which empties a feedback
     *    loop through the name.
     *  - No outlets.
     *  - Parameter: ``name``, the name to send to, at most 63 characters.
     *    Scoped by the patcher name as ``.s`` is, so it meets the names of
     *    this patcher and of patchers with the same name. A ``set`` is not
     *    saved.
     *
     *  Ordering is the graph's: ``patcherImplementation`` places every
     *  ``~receive`` of this name after this object when that closes no
     *  loop (see ``BuildRenderOrder``).
     */
    PATCHER_CLASS(dSend, YSE::OBJ::D_SEND)
    _DO_MESSAGES
    _DO_CALCULATE

    _PARM_PARSE

    _FLOAT_IN(SetValue)
    _INT_IN(SetIntValue)
    _LIST_IN(SetList)

    // Binds the sender once the patcher (and with it the scope) is known.
    void SetParent(pObject* parent) override;
    // A rename moves the scope: bind to the same name in the new one.
    void OnPatcherRenamed() override;

    /** @brief The name sent to now. Control thread, under the patcher's
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
      sender.bind(nullptr);
    }

    /** @brief The DSP endpoint. */
    DSP::signalBusSender& Sender() {
      return sender;
    }

  private:
    // Bind the sender to the bus for the name in use. Control thread.
    void Rebind();

    DSP::signalBusSender sender;
    signalBusPort port;
    aFlt constant;
    std::string name;
  };

} // namespace PATCHER
} // namespace YSE
