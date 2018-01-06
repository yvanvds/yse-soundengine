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

    /* pNote is a note with a position. It's mainly intended for use with motifs.

    */

    class API pNote : public note {
    public:
      pNote(Flt position, Flt pitch, Flt volume, Flt length, Int channel = 1);
      pNote(const note & object, Flt position = 0.f);

      pNote & setPosition(Flt value);
      Flt getPosition() const;

    private:
      Flt position;
    };

  }
}



#endif  // PNOTE_HPP_INCLUDED
