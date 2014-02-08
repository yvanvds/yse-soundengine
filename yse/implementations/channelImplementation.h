/*
  ==============================================================================

    channelImplementation.h
    Created: 30 Jan 2014 4:21:26pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELIMPLEMENTATION_H_INCLUDED
#define CHANNELIMPLEMENTATION_H_INCLUDED
#include "../headers/types.hpp"
#include "JuceHeader.h"
#include "../dsp/sample.hpp"
#include "../internal/reverbDSP.h"
#include "../classes.hpp"
#include <forward_list>

namespace YSE {
  namespace INTERNAL {

    class output {
    public:
      Flt angle;
      Flt initPan;
      Flt initGain;
      Flt effective;
      Flt ratio;
      Flt finalGain;

      output() : angle(0.f) {}
    };

    class channelImplementation : public Thread {
    public:
      channelImplementation(const String & name);
      ~channelImplementation();

      Bool add(channelImplementation * p);
      Bool add(soundImplementation   * p);
      Bool remove(channelImplementation * p);
      Bool remove(soundImplementation   * p);
      void create();
      void update();

      void clearBuffers();
      void dsp();
      void run(); // threading function which calls dsp()
      void exit(); // exit dsp thread 
      void adjustVolume();
      void buffersToParent();

      channelImplementation& volume(Flt  value);
      channelImplementation& allowVirtualSounds(Bool value);

      Flt  volume();
      Bool allowVirtualSounds();

      channelImplementation& set(UInt count); // use this for custom speaker positions, in combination with the pos function below
      channelImplementation& pos(UInt nr, Flt angle); // set speaker to angle in degrees (-180 -> 180)

      void attachUnderWaterFX();

    private:
      aFlt  newVolume;
      Flt   lastVolume;
      CriticalSection dspActive;

      channelImplementation * parent;

      std::forward_list<channelImplementation*> children;
      std::forward_list<soundImplementation *> sounds;

      std::vector<output> outConf;
      std::vector<DSP::sample> out;

      Bool userChannel; // channel is created by user and not crucial for the system
      Bool allowVirtual;

      channelImplementation** link;
      Bool release;

      friend class soundImplementation;
      friend class channel;
      friend class reverbManager;
      friend class deviceManager;
      friend class channelManager;
      friend class soundManager;
    };

  }
}




#endif  // CHANNELIMPLEMENTATION_H_INCLUDED
