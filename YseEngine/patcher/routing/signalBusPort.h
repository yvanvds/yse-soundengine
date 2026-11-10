#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include "../../dsp/routing/signalBus.hpp"

namespace YSE {
  namespace PATCHER {

    class pObject;

    /**
     *  @brief The name half of ``~send`` and ``~receive`` (issue #1136): the
     *         name the object uses now, and a ``set <name>`` waiting to be
     *         taken.
     *
     *  Rebinding looks a bus up and allocates, while a ``set`` may arrive on
     *  the audio thread. So ``RequestSet`` only copies the name into a fixed
     *  slot and asks the patcher for a signal-bus sync
     *  (``patcherImplementation::RequestSignalBusSync``). The sync runs on
     *  the background pool under the patcher's lock: it calls ``TakePending``
     *  on every ``~send`` / ``~receive``, rebinds the ones whose name
     *  changed, and rebuilds the graph so the new pairs are ordered.
     *
     *  The slot is guarded by a flag that the writer only tries: a ``set``
     *  that finds another one being written is dropped and logged rather
     *  than waited for. Two ``set`` messages to one object at the same
     *  instant from two threads is the only way to get there.
     */
    class signalBusPort {
    public:
      /** @brief The longest name, as for ``.s`` and ``.r``. */
      static constexpr std::size_t MAX_NAME = 63;

      /** @brief The creation name. Refused (logged, and the object uses no
       *         name) when longer than ``MAX_NAME``. Clears a waiting
       *         ``set``. Control thread. */
      void Reset(std::string& name, const char* type);

      /** @brief The name in use. Control thread, under the patcher's lock. */
      const std::string& Name() const {
        return current;
      }

      /** @brief The bus for the name in use, scoped by @p parent (a
       *         ``patcherImplementation``), or null without a name or a
       *         parent. Control thread. */
      std::shared_ptr<DSP::signalBus> Bus(const pObject* parent) const;

      /** @brief When @p message is ``set [name]``: stores the name (none
       *         clears it), asks @p owner's patcher for a sync and returns
       *         true. False for any other message. Any thread; allocates
       *         nothing, takes no lock. */
      bool RequestSet(pObject& owner, const std::string& message);

      /** @brief Takes a waiting ``set``. True when the name changed.
       *         Control thread, under the patcher's lock (the sync). */
      bool TakePending();

      /** @brief True when @p message's first word is @p word. */
      static bool FirstWordIs(const std::string& message, const char* word);

    private:
      std::string current;
      char pending[MAX_NAME + 1] = {};
      std::size_t pendingLength = 0;
      std::atomic<bool> writing{false};
      std::atomic<bool> waiting{false};
    };

  } // namespace PATCHER
} // namespace YSE
