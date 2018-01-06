/*
  ==============================================================================

    system.hpp
    Created: 27 Jan 2014 7:14:31pm
    Author:  yvan

  ==============================================================================
*/

#ifndef SYSTEM_H_INCLUDED
#define SYSTEM_H_INCLUDED

#include "headers/types.hpp"
#include "headers/enums.hpp"
#include "utils/vector.hpp"
#include "classes.hpp"

namespace YSE {

  typedef float(*occlusionFunc)(const Pos& source, const Pos& listener);

  class API system {
  public:
    system();

    bool init();
    void update();
    void close();

    /** Get a reference to the global reverb object. It's not active by default,
        but when enabled, these reverb settings will be used when there's no
        other reverb active at the current position. If one or more reverbs are
        partially active (rolloff distance) the global reverb is added partially.
    */
    reverb & getGlobalReverb();

    // This function gets you a list of all available audio devices, but it will only work
    // with YSE as a static library, not with dynamic libraries.
    
    const std::vector<device> & getDevices();
    
    // If YSE is used as a dynamic library, the following functions should be used
    // to retrieve information about devices.
    unsigned int getNumDevices();
    const device & getDevice(unsigned int nr);
    
    void openDevice(const deviceSetup & object, CHANNEL_TYPE conf = CT_AUTO);
    void closeCurrentDevice();

    const char * getDefaultDevice();
    const char * getDefaultHost();

    // effects
    //void insideCave(Bool status);

    // sound occlusion
    /* you should provide your own function for occlusion checks.
      Assuming that your game uses physics, this is quite easy to implement.
      All you have to do is a raycast form the first to the second position and see
      if there are any objects inbetween that should occlude the sound and decide how
      much you want to occlude it.
    */
    system& occlusionCallback(float(*func)(const YSE::Pos&, const YSE::Pos&));
    occlusionFunc occlusionCallback();

    system & underWaterFX(const channel & target);
    system & setUnderWaterDepth(float value);


    // config
    //system& dopplerScale(Flt scale);	Flt dopplerScale();
    //system& distanceFactor(Flt factor);	Flt distanceFactor();
    //system& rolloffScale(Flt scale);	Flt rolloffScale();
    system& maxSounds(int value);	int maxSounds(); // the maximum amount of sounds that are actually used. If the number of sounds exeeds this, the least significant ones will be turned virtual

    system& AudioTest(bool on);

    // statistics
    float cpuLoad(); // cpu load of the audio steam (not the YSE update system)
    void sleep(unsigned int ms); // usefull for console applications if you don't want to run update at max speed
  private:
    float(*occlusionPtr)(const Pos& source, const Pos& listener);
  };

  API system & System();
}



#endif  // SYSTEM_H_INCLUDED
