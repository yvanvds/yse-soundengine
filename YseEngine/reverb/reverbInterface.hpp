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

  namespace REVERB {
    class managerObject;
    class implementationObject;
  }

  /**
     Reverb objects are actually just a collection of reverb settings. At the and of the DSP
     chain the actual reverb object will look at all reverb settings that are close enough to
     an the listener to have an impact. A final setting will then be created that takes all
     other settings and their position into account. This generated setting is passed to
     the actual reverb.

     This technique makes if possible to have a general reverb setting and assign other
     reverb settings to specified positions.
     */
  class API reverb {
  public:
    /**
    Creates a reverb object.
    The global option should only be enabled internally!
    */
    reverb(bool global = false);
    ~reverb();

    /**
     Needed for internal setup of the reverb. This must be done after System().init() and before
     doing anything else with this object
     */
    void create();
    bool isValid();

    /**
     Set the virtual position of this reverb setting.
     */
    reverb& setPosition(const Pos &value);

    /**
     Get the position of this reverb setting.
     */
    Pos getPosition();

    /**
     The size of a reverb defines how far from the center position the settings will
     taken into account at full effect.
     */
    reverb& setSize(float value);

    /**
     Gets the size at which the reverb will have full effect.
     */
    float getSize();

    /**
     Reverb RollOff defines how long it takes for a reverb setting to go from full to
     zero effect. So counting from the counter position there are two zones. Within the
     first zone a reverb is fully in effect. That zone is assigned with the setSize() function.
     This one defines the RollOff, which is the 'fade out' part of the reverb setting.
     */
    reverb& setRollOff(float value);

    /**
     Get the current RollOff value
     */
    float getRollOff();

    /**
     Turn the reverb setting on or off.
     */
    reverb& setActive(bool value);

    /**
     Find out if the reverb is active at the moment.
     */
    bool getActive();

    /**
     Set the roomsize.
     */
    reverb& setRoomSize(float value);

    /**
     Get the current roomsize.
     */
    float getRoomSize();

    /**
      Set the current damping value for this reverb.
      */
    reverb& setDamping(float value);

    /**
     Get the current damping value.
     */
    float getDamping();


    /**
     This defines how much of the processed signal actually
     makes it to the output of the reverb. In most circumstances
     the dry and wet should add up to 1. If the sum is > 1 you
     might get a distorted sound.

     @param dry Defines how much of the original signal will be
     left in the output signal.
     @param wet Defines how much of the reverb should be added
     to the output signal.

     */
    reverb& setDryWetBalance(float dry, float wet);

    /**
     Get the 'wet' value for this reverb.
     */
    float getWet();

    /**
     Get the 'dry' value for this reverb.
     */
    float getDry();

    /**
     This function modulates the processed reverb signal at a
     certain frequency.

     @param frequency The frequency at which to modulate the signal.
     @param width The modulation width.
     */
    reverb& setModulation(float frequency, float width);

    /**
     Get the modulation frequency for this reverb.
     */
    float getModulationFrequency();

    /**
     Get the modulation with for this reverb.
     */
    float getModulationWidth();

    /**
     In addition to the typical 'blurred' reverb output, you can add
     up to 4 reflections. Every reflection can have its own gain
     and time. This function sets the time for a reflection.

     @param reflection The number of the reflection you'd like to change. Must be in
     the range [0-3]
     @param time       The time to which this reflection should be set.
     @param gain       The gain of the specified reflection.
     */
    reverb& setReflection(int reflection, int time, float gain);

    /**
      Get the time of the specified reflection.

      @param reflection The number of the reflection. Must be in the range [0-3]
      */
    int getReflectionTime(int reflection);

    /**
     Get the gain of the specified reflection.

     @param reflection The number of the reflection. Must be in the range [0-3]
     */
    float getReflectionGain(int reflection);

    /**
     Set the reverb settings to a predefined preset.
     */
    reverb& setPreset(REVERB_PRESET value);

    /**
     Release the reverb object. It is not really needed to call this, because it will
     be called at desctruction time anyway. But you could. It renders the object invalid.
     */
    //reverb& release();



  private:
    REVERB::implementationObject * pimpl;

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

}



#endif  // REVERB_H_INCLUDED
