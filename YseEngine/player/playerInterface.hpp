/*
  ==============================================================================

    playerInterface.hpp
    Created: 9 Apr 2015 1:38:34pm
    Author:  yvan

  ==============================================================================
*/

#ifndef PLAYERINTERFACE_HPP_INCLUDED
#define PLAYERINTERFACE_HPP_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "player.hpp"
#include "../synth/synth.hpp" // YSE::synth (create target) (#156)
#include "../music/motif/motif.hpp"

namespace YSE {

  /**
   *  @brief Generative note sequencer.
   *
   *  Plays random notes within configurable pitch / velocity / gap / length
   *  ranges, optionally constrained to a ``scale``, optionally drawing from
   *  one or more weighted ``motif`` patterns instead of pure randomness. All
   *  numeric setters accept an optional ``time`` parameter that linearly
   *  interpolates from the current value to the target over that many seconds.
   *
   *  @see YSE::scale
   *  @see YSE::motif
   */
  class API player {
  public:
    player();
    ~player();

    /**
     *  @brief Connect this player to a ``synth`` and register it with the engine.
     *
     *  Every note the player generates is delivered to ``instrument`` through its
     *  public note API (the synth's lock-free inbox), so the same synth must not
     *  be driven concurrently from another source. Must be called before
     *  ``play()`` or any setter; calling those first is a no-op that logs a
     *  warning rather than crashing. ``instrument`` must outlive this player.
     *  Calling ``create`` twice is a no-op.
     */
    player& create(synth& instrument);

    /** @brief Start producing notes. */
    player& play();

    /** @brief Stop producing notes. */
    player& stop();

    /** @brief Whether the player is currently producing notes. */
    Bool isPlaying();

    /** @brief Set the lowest pitch the player may produce. Range [0, 126]. */
    player& setMinimumPitch(Flt target, Flt time = 0);

    /** @brief Set the highest pitch the player may produce. Range [1, 127]. */
    player& setMaximumPitch(Flt target, Flt time = 0);

    /** @brief Set the lowest velocity. Range [0, 0.999999]. */
    player& setMinimumVelocity(Flt target, Flt time = 0);

    /** @brief Set the highest velocity. Range [0.000001, 1]. */
    player& setMaximumVelocity(Flt target, Flt time = 0);

    /** @brief Set the minimum gap between successive notes / motifs, in seconds. */
    player& setMinimumGap(Flt target, Flt time = 0);

    /** @brief Set the maximum gap between successive notes / motifs, in seconds. */
    player& setMaximumGap(Flt target, Flt time = 0);

    /** @brief Set the minimum note length, in seconds. Used when no motif is active. */
    player& setMinimumLength(Flt target, Flt time = 0);

    /** @brief Set the maximum note length, in seconds. Used when no motif is active. */
    player& setMaximumLength(Flt target, Flt time = 0);

    /** @brief Set the number of simultaneous voices. */
    player& setVoices(UInt target, Flt time = 0);

    /**
     *  @brief Constrain generated pitches to a scale.
     *  @note The player keeps its own copy — modifying ``scale`` after this
     *        call has no effect on the player.
     */
    player& setScale(scale& scale, Flt time = 0);

    /**
     *  @brief Add a motif to the player's pool.
     *
     *  When the player decides to play a motif (see ``playMotifs``) it picks
     *  one weighted by ``weight``.
     */
    player& addMotif(motif& motif, UInt weight = 1);

    /** @brief Remove a previously added motif. */
    player& removeMotif(motif& motif);

    /** @brief Adjust the selection weight of an already-added motif. */
    player& adjustMotifWeight(motif& motif, UInt weight);

    /**
     *  @brief Probability that the player plays only part of a motif.
     *
     *  ``target == 0`` always plays full motifs, ``target == 1`` always plays
     *  partial motifs, values in between mix the two.
     */
    player& playPartialMotifs(Flt target, Flt time = 0);

    /**
     *  @brief Probability that the player draws notes from a motif vs. random.
     *
     *  ``target == 0`` is pure random, ``target == 1`` is motifs only.
     */
    player& playMotifs(Flt target, Flt time = 0);

    /**
     *  @brief Probability that motif notes are quantised to the active scale.
     *
     *  ``target == 0`` plays motifs as written (only the first note is forced
     *  onto the scale); ``target == 1`` snaps every note to the scale.
     */
    player& fitMotifsToScale(Flt target, Flt time = 0);

  private:
    // Guard against use-before-create: returns true when a live implementation
    // is attached, otherwise logs a warning and returns false so the caller can
    // return early instead of dereferencing a null pimpl (#156 / #268).
    bool checkCreated() const;

    PLAYER::implementationObject* pimpl;
    Bool _isPlaying;

    friend class PLAYER::implementationObject;
  };

} // namespace YSE

#endif // PLAYERINTERFACE_HPP_INCLUDED
