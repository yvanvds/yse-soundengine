#pragma once
#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include "../pObject.h"

namespace YSE {
  namespace PATCHER {

    /**
     *  @brief What ``~dac``, ``~adc``, ``~ezdac`` and ``~ezadc`` share: a
     *         channel route per signal pin, and the patcher's DSP switch
     *         (issue #1043).
     *
     *  This is graph I/O, not signal processing, so there is no ``YSE::DSP``
     *  class behind these objects: the two-layer rule (#1035) is waived. The
     *  patcher reads the routes when it mixes the ``~dac`` objects into its
     *  output and when it feeds the host's audio to the ``~adc`` objects.
     *
     *  **Routes.** Each signal pin (a ``~dac`` inlet, an ``~adc`` outlet) has
     *  a channel number from 1, or 0 for none. The arguments set them when the
     *  object is built; ``set`` and a list change them later. They are atomics
     *  in an array sized when the pins are built, so a message on any thread
     *  stores one int and the audio thread loads it. A channel the patcher (or
     *  the host buffer) does not have is silent, like 0.
     *
     *  **DSP switch.** ``start``, ``stop``, ``startwindow`` and a number turn
     *  the owning patcher's DSP on or off (patcherImplementation::SetDspOn):
     *  one atomic store, never a wait on the audio thread, which reads the
     *  flag at the top of the next block. Every patcher starts with DSP on, so
     *  a patch that never sends ``stop`` plays as it always did.
     *
     *  **Saving.** The arguments are saved; ``set``, lists and the DSP state
     *  are not.
     */
    class pAudioIO : public pObject {
    public:
      /** @brief Highest channel number a route takes; above it is none. */
      static constexpr int MAX_CHANNEL = 1024;
      /** @brief Most signal pins one object builds. */
      static constexpr int MAX_PINS = 1024;

      /** @brief Number of routed signal pins. */
      unsigned int NumRoutes() const {
        return routeCount;
      }

      /** @brief The channel (from 0) pin @p pin is routed to, or -1 for none
       *         or a pin out of range. Any thread; one relaxed load. */
      int Route(unsigned int pin) const;

      /** @brief Routes pin @p pin to @p channel, counted from 1 (0, or one
       *         out of range, is none). Out-of-range pins are ignored. Any
       *         thread; one relaxed store. */
      void SetRoute(unsigned int pin, int channel);

      /** @brief Whether the owning patcher's DSP is on; true outside a
       *         patcher. */
      bool DspOn() const;

    protected:
      // `defaultChannels` is the pin count with no arguments: the patcher's
      // channel count in a live graph, 2 in the registry's metadata object.
      explicit pAudioIO(int defaultChannels);

      // Turns the owning patcher's DSP on or off. No-op outside a patcher.
      void SetDspOn(bool on);

      // The DSP switch shared by every pin that takes messages: a number
      // (0 is off), start / startwindow / stop. True when `value` was one of
      // them. Walked in place, allocation-free, any thread.
      bool HandleSwitch(const std::string& value);
      void HandleSwitchNumber(float value);

      // Routes from `firstPin` on, one per number in `text` starting at
      // character `from`; stops at the first token that is not a number.
      void SetRoutesFrom(unsigned int firstPin, const std::string& text, std::size_t from);

      // Reads the creation arguments into `channels` (from 1): one channel per
      // number, or `defaultChannels` channels 1..N without a number. With
      // `singleIsCount`, one number N alone means channels 1..N (~adc).
      // Control thread, construction only.
      void ArgumentChannels(std::vector<int>& channels, bool singleIsCount) const;

      // Allocates the routes for `channels.size()` pins and stores them.
      // Construction only, before anything is wired or published.
      void BuildRoutes(const std::vector<int>& channels);

      // The first token at or after `from`, as bounds into `text`; false when
      // there is none.
      static bool NextToken(const std::string& text, std::size_t from, std::size_t& begin,
                            std::size_t& end);
      static bool TokenIs(const std::string& text, std::size_t begin, std::size_t end,
                          const char* word);

      int defaultChannels;
      std::vector<std::string> creationArgs;

    private:
      std::unique_ptr<std::atomic<int>[]> routes;
      unsigned int routeCount = 0;
    };

  } // namespace PATCHER
} // namespace YSE
