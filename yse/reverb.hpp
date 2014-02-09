/*
  ==============================================================================

    reverb.h
    Created: 1 Feb 2014 7:02:58pm
    Author:  yvan

  ==============================================================================
*/

#ifndef REVERB_H_INCLUDED
#define REVERB_H_INCLUDED

#include "utils/vector.hpp"
#include "headers/enums.hpp"

namespace YSE {
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
     Set the virtual position of this reverb setting.
    */
    reverb& setPosition(const  Vec &value);
    
    /**
     Get the position of this reverb setting.
    */
    Vec getPosition();
    
    /**
     The size of a reverb defines how far from the center position the settings will 
     taken into account at full effect.
    */
    reverb& setSize(Flt  value);
    
    /**
     Gets the size at which the reverb will have full effect.
    */
    Flt getSize();
    
    /**
     Reverb RollOff defines how long it takes for a reverb setting to go from full to
     zero effect. So counting from the counter position there are two zones. Within the 
     first zone a reverb is fully in effect. That zone is assigned with the setSize() function.
     This one defines the RollOff, which is the 'fade out' part of the reverb setting.
    */
    reverb& setRollOff(Flt  value);
      
    /**
     Get the current RollOff value
    */
    Flt getRollOff();
      
    /**
     Turn the reverb setting on or off.
    */
    reverb& setActive(Bool value);
      
    /**
     Find out if the reverb is active at the moment.
    */
    Bool getActive();
    
    /**
     Set the roomsize.
    */
    reverb& setRoomSize(Flt  value);
    
    /**
     Get the current roomsize.
    */
    Flt getRoomSize();
      
    /**
      Set the current damping value for this reverb.
    */
    reverb& setDamping(Flt  value);
    
    /**
     Get the current damping value.
    */
    Flt getDamping();
    
      
    /**
     Set the 'wet' value for this reverb. This defines how much
     of the processed signal actually makes it to the output of
     the reverb.
    */
    reverb& setWet(Flt  value);
      
    /**
     Get the 'wet' value for this reverb.
    */
    Flt getWet();
      
    /**
     Set the 'dry' value for this reverb. This defines how much
     of the orginal signal is mixed into the output of the 
     reverb object.
    */
    reverb& setDry(Flt  value);
      
    /**
     Get the 'dry' value for this reverb.
    */
    Flt getDry();
      
    /**
     This function modulates the processed reverb signal at a 
     certain frequency.
     
     @param value the frequency at which to modulate the signal
    */
    reverb& setModulationFrequency(Flt  value);
      
    /**
     Get the modulation frequency for this reverb.
    */
    Flt getModulationFrequency();
      
    /**
     Set the width of the modulation applied to this reverb. Use 
     this function together with setModulationFrequency() to fully
     control the modulation behaviour of the reverb.
    */
    reverb& setModulationWidth(Flt  value);
    
    /**
     Get the modulation with for this reverb.
    */
    Flt getModulationWidth();

    /**
     In addition to the typical 'blurred' reverb output, you can add 
     up to 4 reflections. Every reflection can have its own gain
     and time. This function sets the time for a reflection.
     
     @param reflection The number of the reflection you'd like to change. Must be in
                       the range [0-3]
     @param time       The time to which this reflection should be set.
     @param gain       The gain of the specified reflection.
    */
    reverb& setReflection(Int reflection, Int time, Int gain);
      
    /** 
      Get the time of the specified reflection. 
     
      @param reflection The number of the reflection. Must be in the range [0-3]
    */
    Int getReflectionTime(Int reflection);
    
    /**
     Get the gain of the specified reflection.
     
     @param reflection The number of the reflection. Must be in the range [0-3]
    */
    Flt getReflectionGain(Int reflection);

    /**
     Set the reverb settings to a predefined preset.
    */
    reverb& preset(REVERB_PRESET value);

    /**
     Release the reverb object. It is not really needed to call this, because it will
     be called at desctruction time anyway. But you could. It renders the object invalid.
    */
    reverb& release();

    reverb();
   ~reverb();

  private:
    Bool _active;
    Bool connectedToManager; 
    Flt _roomsize, _damp, _wet, _dry;

    Flt _modFrequency, _modWidth; // modulation

    Int _earlyPtr[4]; // early reflections
    Flt _earlyGain[4];

    Vec _position;
    Flt _size, _rolloff;

    REVERB_PRESET _preset;

    Bool global;
    friend class INTERNAL::reverbManager;
  };

  /**
   Functor to the global reverb object. This is a reverb setting that travels with the listener instead of being stationary at one spot.
   Settings of this reverb will affect the resulting reverb whenever local reverbs do not accumulate to 100% reverb activity.
  */
  reverb & GlobalReverb();
}



#endif  // REVERB_H_INCLUDED
