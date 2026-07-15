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

  /**
   *  @brief A sequence of positioned notes — a re-usable phrase or pattern.
   *
   *  Add notes (with positions) via ``add``, optionally constrain the
   *  starting pitch with a ``scale``, and hand the motif to a ``player``
   *  which will trigger it at appropriate moments.
   *
   *  @see YSE::MUSIC::pNote
   *  @see YSE::player
   */
  class API motif {
  public:
    motif();
    ~motif();

    /** @brief Append a positioned note. */
    motif& add(const MUSIC::pNote& note);

    /** @brief Remove every note. */
    motif& clear();

    /** @brief Set the motif length explicitly, in seconds. */
    motif& setLength(Flt length);

    /** @brief Set the motif length automatically to the end of the last note. */
    motif& setLength();

    /** @brief Transpose every note by ``pitch`` semitones. */
    motif& transpose(Flt pitch);

    /**
     *  @brief Restrict legal starting pitches.
     *
     *  When a player picks a transposition for this motif it picks one whose
     *  starting note belongs to ``validPitches``.
     */
    motif& setFirstPitch(const scale& validPitches);

    /** @brief Current motif length in seconds. */
    Flt getLength() {
      return length;
    }

    /** @brief Whether the motif contains any notes. */
    Bool empty() {
      return notes.empty();
    }

    /** @brief Number of notes. */
    UInt size() {
      return (UInt)notes.size();
    }

    /** @brief Access the note at index ``pos``. */
    MUSIC::pNote& operator[](UInt pos);

    motif(const motif& other);
    motif& operator=(const motif& other);

  private:
    // order notes according to position
    void sort();

    MOTIF::implementationObject* pimpl;

    Flt length;
    std::vector<MUSIC::pNote> notes;

    friend class MOTIF::implementationObject;
    friend class player;
  };

} // namespace YSE

#endif // MOTIFINTERFACE_H_INCLUDED
