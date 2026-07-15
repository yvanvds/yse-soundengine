/*
  ==============================================================================

    perChannel.hpp
    Per-channel state fan-out helper for multichannel dspObject modules.

  ==============================================================================
*/

#ifndef PERCHANNEL_HPP_INCLUDED
#define PERCHANNEL_HPP_INCLUDED

#include <cstddef>
#include <utility>
#include <vector>

namespace YSE {
  namespace DSP {

    /**
     *  @brief Per-channel state fan-out for multichannel ``dspObject`` modules.
     *
     *  A ``dspObject::process(MULTICHANNELBUFFER&)`` must process every channel
     *  independently (see the contract on ``dspObject::process``). Any state
     *  that remembers per-sample history — filter memory, delay lines,
     *  oscillator phase — must exist *once per channel*, otherwise state bleeds
     *  from one channel into the next and the channels are no longer
     *  independent.
     *
     *  This utility lets a module be written as "one channel's worth of state"
     *  (a small ``State`` struct) and then be fanned out to N channels without
     *  hand-rolling a ``std::vector`` and its resize bookkeeping in every
     *  module. It mirrors the pattern ``reverbDSP`` uses with its
     *  ``std::vector<reverbChannel>``.
     *
     *  @tparam State  A per-channel state bundle. Must be default-constructible
     *                 (``ensure`` grows the vector by default-constructing new
     *                 states) and copyable/movable (``std::vector`` may relocate
     *                 existing states when it grows — copying preserves their
     *                 history).
     *
     *  ### Real-time discipline
     *
     *  ``ensure`` is the *only* member that can allocate, and it only does so
     *  when the requested channel count differs from the current one — i.e. on
     *  ``create()`` and on the device-restart resize path, never in steady
     *  state. Call ``ensure(buffer.size())`` at the top of ``process`` and it
     *  is a no-op (returns ``false``, no allocation) on every steady-state
     *  callback. All other members are allocation-free.
     */
    template <typename State> class perChannel {
    public:
      /** @brief Number of channel states currently held. */
      std::size_t size() const {
        return states.size();
      }

      /** @brief Whether no channel states are held yet. */
      bool empty() const {
        return states.empty();
      }

      /** @brief Access the state for channel ``i`` (no bounds check). */
      State& operator[](std::size_t i) {
        return states[i];
      }

      /** @brief Const access to the state for channel ``i`` (no bounds check). */
      const State& operator[](std::size_t i) const {
        return states[i];
      }

      /** @brief Grow or shrink to ``count`` channel states.
       *
       *  Returns ``true`` if the channel count changed — in which case an
       *  allocation may have occurred, so this must only be reached off the
       *  steady-state audio path (from ``create()`` or the device-restart
       *  resize path). New states are default-constructed; existing states keep
       *  their history (a shrink drops the tail, a subsequent grow appends
       *  fresh states after the retained prefix). Returns ``false`` and does
       *  nothing when ``count`` already matches ``size()``.
       */
      bool ensure(std::size_t count) {
        if (states.size() == count) return false;
        states.resize(count);
        return true;
      }

      /** @brief Grow or shrink to ``count`` channel states, initialising new ones.
       *
       *  Like ``ensure(count)``, but every newly appended state is handed to
       *  ``initEach(State&)`` after construction. Use it to wire up state a
       *  default constructor cannot reach — a shared wavetable pointer, a
       *  filter's resonance, etc. ``initEach`` runs only on the states that
       *  were just added, never on retained ones. Same allocation contract as
       *  ``ensure(count)``.
       */
      template <typename InitFn> bool ensure(std::size_t count, InitFn&& initEach) {
        if (states.size() == count) return false;
        std::size_t previous = states.size();
        states.resize(count);
        for (std::size_t i = previous; i < states.size(); ++i)
          initEach(states[i]);
        return true;
      }

    private:
      std::vector<State> states;
    };

  } // namespace DSP
} // namespace YSE

#endif // PERCHANNEL_HPP_INCLUDED
