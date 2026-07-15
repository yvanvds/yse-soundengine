/*
  ==============================================================================

    reverb.h
    Created: 1 Feb 2014 7:02:58pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERBINTERFACE_H_INCLUDED
#define REVERBINTERFACE_H_INCLUDED

#include "../headers/defines.hpp"
#include "../headers/enums.hpp"
#include "../utils/vector.hpp"

namespace YSE {

  /// @cond INTERNAL
  namespace REVERB {
    class managerObject;
    class implementationObject;
  } // namespace REVERB
  /// @endcond

  /**
   *  @brief A positioned reverb zone.
   *
   *  Each ``reverb`` object holds a set of parameters and a position in the
   *  scene. At the end of every DSP frame the engine looks at every reverb
   *  whose rolloff radius overlaps the listener and blends their parameters
   *  by proximity into the single shared reverb processor. The effect: you
   *  can drop multiple reverb zones around the world (cave, hall, bathroom)
   *  and the listener smoothly transitions between them as they move.
   *
   *  A "global" reverb is also available through ``System().getGlobalReverb()``;
   *  it is mixed in as the fallback wherever no positioned reverb reaches.
   *
   *  @see YSE::System
   *  @see YSE::REVERB_PRESET
   */
  class API reverb {
  public:
    /**
     *  @brief Construct a reverb zone.
     *
     *  @param global Reserved for the engine — leave as ``false`` in user code.
     *                ``true`` is used internally by ``System().getGlobalReverb()``.
     */
    reverb(bool global = false);
    ~reverb();

    /**
     *  @brief Copy-assignment is deleted.
     *
     *  A ``reverb`` owns a raw ``pimpl`` that the engine tracks by interface
     *  identity. Copy-assigning would alias two interfaces onto a single
     *  implementation and turn that impl's single-producer/single-consumer
     *  message queue into a dual-producer queue on the audio thread
     *  (issue #192). Copying a reverb is never valid — forbid it.
     */
    reverb& operator=(const reverb&) = delete;

    /** @brief Initialise the reverb.
     *
     *  Must be called after ``System().init()`` and before any other method on
     *  this object.
     */
    void create();

    /** @brief Whether this reverb has a live implementation. */
    bool isValid();

    /** @brief Set the position of the reverb zone in the scene. */
    reverb& setPosition(const Pos& value);

    /** @brief Current zone position. */
    Pos getPosition();

    /**
     *  @brief Radius within which the reverb is at full strength.
     *
     *  Inside this radius the zone is applied fully; beyond it, the strength
     *  fades over the rolloff distance (see ``setRollOff``).
     */
    reverb& setSize(float value);

    /** @brief Current full-strength radius. */
    float getSize();

    /**
     *  @brief Distance over which the reverb fades out.
     *
     *  Measured from the edge of the full-strength radius. Outside
     *  ``size + rollOff`` from the center, this zone contributes nothing.
     */
    reverb& setRollOff(float value);

    /** @brief Current rolloff distance. */
    float getRollOff();

    /** @brief Enable or disable this reverb zone. */
    reverb& setActive(bool value);

    /** @brief Whether this reverb zone is currently active. */
    bool getActive();

    /** @brief Set the simulated room size. Larger values give longer tails. */
    reverb& setRoomSize(float value);

    /** @brief Current room size. */
    float getRoomSize();

    /** @brief Set the high-frequency damping.
     *
     *  Higher damping makes the reverb tail darken faster, simulating soft
     *  materials.
     */
    reverb& setDamping(float value);

    /** @brief Current damping value. */
    float getDamping();

    /**
     *  @brief Set the dry/wet balance.
     *
     *  @param dry How much of the source signal passes through unprocessed.
     *  @param wet How much of the reverberated signal is mixed in.
     *
     *  @note ``dry + wet`` should usually be 1.0. Sums above 1.0 can clip.
     */
    reverb& setDryWetBalance(float dry, float wet);

    /** @brief Current wet level. */
    float getWet();

    /** @brief Current dry level. */
    float getDry();

    /**
     *  @brief Modulate the reverb tail.
     *
     *  Adds a slow LFO to the reverb output to break up metallic resonances.
     *
     *  @param frequency Modulation rate in Hz.
     *  @param width     Modulation depth.
     */
    reverb& setModulation(float frequency, float width);

    /** @brief Current modulation frequency. */
    float getModulationFrequency();

    /** @brief Current modulation width. */
    float getModulationWidth();

    /**
     *  @brief Configure one of the four early reflections.
     *
     *  Layered on top of the diffuse reverb tail to give the perception of
     *  nearby reflective surfaces.
     *
     *  @param reflection Reflection index in [0, 3].
     *  @param time       Delay time of this reflection.
     *  @param gain       Gain of this reflection.
     */
    reverb& setReflection(int reflection, int time, float gain);

    /** @brief Delay time of the given reflection. ``reflection`` is in [0, 3]. */
    int getReflectionTime(int reflection);

    /** @brief Gain of the given reflection. ``reflection`` is in [0, 3]. */
    float getReflectionGain(int reflection);

    /** @brief Apply a named preset (cave, hall, bathroom, ...).
     *  @see YSE::REVERB_PRESET
     */
    reverb& setPreset(REVERB_PRESET value);

  private:
    REVERB::implementationObject* pimpl;

    Bool connectedToManager;
    Bool active;

    Flt roomsize, damp, wet, dry;

    Flt modFrequency, modWidth; // modulation

    Int earlyPtr[4]; // early reflections
    Flt earlyGain[4];

    Pos position;
    Flt size, rolloff;

    REVERB_PRESET preset;

    Bool global;
    friend class REVERB::managerObject;
    friend class REVERB::implementationObject;
  };

} // namespace YSE

#endif // REVERB_H_INCLUDED
