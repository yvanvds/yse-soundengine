#pragma once
#include "../headers/defines.hpp"
#include "midiMessage.hpp"

namespace YSE {
  namespace MIDI {

    /**
     *  @brief A MIDI note-on/note-off message.
     *
     *  Convenience wrapper around the raw byte sequence — exposes the
     *  note number and velocity as named fields.
     */
    class API midiNote : public midiMessage {
    public:
      /** @brief Construct a note message.
       *  @param note     MIDI note number (0-127).
       *  @param velocity Velocity (0-127).
       */
      midiNote(unsigned char note, unsigned velocity);

      /** @brief Set the MIDI note number. */
      void note(unsigned char note);

      /** @brief Current MIDI note number. */
      unsigned char note() const;

      /** @brief Set the velocity. */
      void velocity(unsigned char velocity);

      /** @brief Current velocity. */
      unsigned char velocity() const;
    };

  } // namespace MIDI
} // namespace YSE