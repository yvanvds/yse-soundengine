/*
  ==============================================================================

    scaleInterface.h
    Created: 14 Apr 2015 2:54:43pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SCALEINTERFACE_H_INCLUDED
#define SCALEINTERFACE_H_INCLUDED

#include "../../headers/defines.hpp"
#include "../../headers/types.hpp"
#include "scale.hpp"
#include "../motif/motif.hpp"
#include <vector>

namespace YSE {

  class API  scale {
  public:
    scale();
    ~scale();

    // Add a pitch to the scale. By default it will be added at every
    // octave, but this can be changed with the step value. (A step <= 0
    // means adding only this exact pitch, without transpositions.)
    scale & add(Flt pitch, Flt step = 12);

    // Remove a pitch from the scale. By default it will be removed at every
    // octave, but this can be changed with the step value.
    scale & remove(Flt pitch, Flt step = 12);

    // Check if a pitch is part of the scale
    Bool has(Flt pitch);
 
    // Get the nearest match for this pitch
    Flt getNearest(Flt pitch) const;
 
    // count notes in this scale
    UInt size() const;

    // remove all notes
    scale & clear();
 
    // copy
    scale(const scale& other);
    scale & operator=(const scale & other);
        
  private:
    SCALE::implementationObject * pimpl;

    // keep list of pitches in the interface as well as in the 
    // implementation, because we need to be able to copy them.
    std::vector<Flt> pitches;

    friend class SCALE::implementationObject;
    friend class player;
    friend class motif;
  };

}



#endif  // SCALEINTERFACE_H_INCLUDED
