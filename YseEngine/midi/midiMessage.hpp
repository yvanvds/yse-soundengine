#pragma once
#include "../headers/defines.hpp"

namespace YSE {
  namespace MIDI {

    /**
     *  @brief Base class for MIDI messages.
     *
     *  Stores the raw MIDI byte sequence. Subclassed by ``midiNote`` and
     *  other message types; the raw bytes can be inspected through
     *  ``getRaw``.
     */
    class API midiMessage {
    public:
      /** @brief Pointer to the raw MIDI byte sequence backing this message. */
      inline const std::vector<unsigned char>* getRaw() const {
        return &raw;
      }

    protected:
      std::vector<unsigned char> raw;
    };

  } // namespace MIDI
} // namespace YSE