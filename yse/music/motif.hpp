/*
  ==============================================================================

    motif.hpp
    Created: 11 Apr 2015 1:04:24pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MOTIF_HPP_INCLUDED
#define MOTIF_HPP_INCLUDED

#include "../classes.hpp"
#include "../headers/defines.hpp"
#include <vector>

namespace YSE {
  namespace MUSIC {

    /* motifs are chains of positioned notes (pNote) which can be passed to players.
    */

    class API motif {
    public:
      motif() : length(0) {}

      motif & add(pNote & note);
      motif & sort();
      motif & clear();

      // manualy set the length of this motif.
      motif & setLength(Flt length);

      // automatically set the length of this motif to the position of the last note + its length
      motif & setLength();

      Flt getLength() { return length; }

      // Get the number of notes starting between startPos and endPos. Optionally
      // a pointer to the first note equal or after startPos can be retrieved. If the 
      // motif is sorted (and it should be!) the other notes are next to the first element.
      Int getNotes(Flt startPos, Flt endPos, pNote * firstElement = nullptr);

    private:
      Flt length;
      std::vector<pNote> notes;
    };

  }
}



#endif  // MOTIF_HPP_INCLUDED
