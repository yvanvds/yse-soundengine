/*
  ==============================================================================

    motifInterface.h
    Created: 14 Apr 2015 6:18:45pm
    Author:  yvan

  ==============================================================================
*/

#ifndef MOTIFINTERFACE_H_INCLUDED
#define MOTIFINTERFACE_H_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "../../classes.hpp"
#include "motif.hpp"
#include <vector>

namespace YSE {

    class API  motif {
    public:
      motif();
      ~motif();

      motif & add(const MUSIC::pNote & note);

      motif & clear();

      // manualy set the length of this motif.
      motif & setLength(Flt length);

      // automatically set the length of this motif to the position of the last note + its length
      motif & setLength();

      // transpose up / down
      motif & transpose(Flt pitch);

      // A scale can be passed with possible starting points for this motif.
      // A player object will use this information to determine valid transpositions.
      motif & setFirstPitch(const scale & validPitches);

      Flt getLength() { return length; }
      Bool empty() { return notes.empty(); }
      UInt size() { return notes.size(); }

      MUSIC::pNote & operator[](UInt pos);

      // copy
      motif(const motif& other);
      motif & operator=(const motif & other);

    private:
      // order notes according to position
      void sort();

      MOTIF::implementationObject * pimpl;

      Flt length;
      std::vector<MUSIC::pNote> notes;

      friend class MOTIF::implementationObject;
      friend class player;
    };

}




#endif  // MOTIFINTERFACE_H_INCLUDED
