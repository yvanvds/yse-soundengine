/*
  ==============================================================================

    pNote.hpp
    Created: 11 Apr 2015 12:38:20pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PNOTE_HPP_INCLUDED
#define PNOTE_HPP_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include "note.hpp"

namespace YSE {
  namespace MUSIC {

    /**
     *  @brief A ``note`` with a time position — the building block of a ``motif``.
     *
     *  Where a plain ``note`` represents a sound event in isolation, ``pNote``
     *  fixes it to a moment in a motif's timeline. Position is measured from
     *  the start of the containing motif, in seconds.
     */
    class API pNote : public note {
    public:
      /**
       *  @brief Construct a positioned note.
       *
       *  @param position Offset from the start of the motif, in seconds.
       *  @param pitch    MIDI pitch (60 = middle C).
       *  @param volume   Velocity in [0.0, 1.0].
       *  @param length   Duration in seconds.
       *  @param channel  MIDI channel.
       */
      pNote(Flt position, Flt pitch, Flt volume, Flt length, Int channel = 1);

      /** @brief Construct from an existing ``note`` plus a position. */
      pNote(const note & object, Flt position = 0.f);

      /** @brief Set the time position. */
      pNote & setPosition(Flt value);

      /** @brief Current time position. */
      Flt getPosition() const;

    private:
      Flt position;
    };

  }
}



#endif  // PNOTE_HPP_INCLUDED
