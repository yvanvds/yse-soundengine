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

  /**
   *  @brief A set of allowed pitches.
   *
   *  Drives the ``player`` so its generated notes stay in key, and constrains
   *  the transpositions a ``motif`` can take. Pitches are added with an
   *  optional step that controls automatic octave replication.
   *
   *  @see YSE::player
   *  @see YSE::motif
   */
  class API  scale {
  public:
    scale();
    ~scale();

    /**
     *  @brief Add a pitch to the scale.
     *
     *  @param pitch The MIDI pitch to add.
     *  @param step  Octave step. ``12`` (default) replicates the pitch at
     *               every octave. Values <= 0 add only the exact pitch.
     */
    scale & add(Flt pitch, Flt step = 12);

    /**
     *  @brief Remove a pitch from the scale.
     *
     *  @param pitch The MIDI pitch to remove.
     *  @param step  Octave step — by default the pitch is removed at every octave.
     */
    scale & remove(Flt pitch, Flt step = 12);

    /** @brief Whether ``pitch`` is a member of the scale. */
    Bool has(Flt pitch);

    /** @brief Nearest in-scale pitch to ``pitch``. */
    Flt getNearest(Flt pitch) const;

    /** @brief Number of pitches in the scale. */
    UInt size() const;

    /** @brief Remove every pitch. */
    scale & clear();

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
