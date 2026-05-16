/*
  ==============================================================================

    listener.h
    Created: 30 Jan 2014 4:22:19pm
    Author:  yvan

  ==============================================================================
*/


#ifndef LISTENER_H_INCLUDED
#define LISTENER_H_INCLUDED

#include "headers/defines.hpp"
#include "utils/vector.hpp"

namespace YSE {

  /**
   *  @brief Singleton representing the listener's position and orientation in the virtual scene.
   *
   *  The Listener defines the reference point used by the engine to pan sounds
   *  across the available speakers, attenuate them by distance, and compute
   *  doppler shifts. Update its position every frame (typically alongside
   *  ``System().update()``) so velocity and doppler stay coherent.
   *
   *  Access through the free function ``Listener()``.
   *
   *  @see YSE::Listener
   *  @see YSE::Pos
   */
  class API listener {
  public:

    /** @brief Current listener position in world coordinates. */
    Pos pos();

    /** @brief Current listener velocity in units per second.
     *
     *  Derived from successive calls to ``pos(const Pos&)`` — it cannot be set
     *  directly. Used internally for doppler calculations.
     */
    Pos vel();

    /** @brief Forward-facing unit vector of the listener. */
    Pos forward();

    /** @brief Upward unit vector of the listener. */
    Pos upward();

    /** @brief Set the listener position.
     *
     *  Call once per frame to keep velocity-based effects (doppler, motion
     *  panning) accurate. Setting the position less frequently is fine for
     *  static scenes but will degrade the velocity estimate.
     */
    listener& pos(const Pos &pos);

    /** @brief Set the listener orientation.
     *
     *  @param forward The direction the listener faces.
     *  @param up      The upward axis. Defaults to (0, 1, 0), i.e. rotation
     *                 confined to a horizontal plane.
     */
    listener& orient(const Pos &forward, const Pos &up = Pos(0, 1, 0));

  };

  /** @brief Access the singleton listener object. */
  API listener & Listener();
}



#endif  // LISTENER_H_INCLUDED
