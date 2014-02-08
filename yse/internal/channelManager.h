/*
  ==============================================================================

    channelManager.h
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELMANAGER_H_INCLUDED
#define CHANNELMANAGER_H_INCLUDED

#include "../channel.hpp"
#include "JuceHeader.h"
#include "../headers/enums.hpp"
#include <forward_list>

namespace YSE {
  namespace INTERNAL {

    class channelManager {
    public:
      void update();
      void changeChannelConf(CHANNEL_TYPE type, Int outputs = 2);
      UInt getNumberOfOutputs();

      channel & mainMix();
      channel & FX();
      channel & music();
      channel & ambient();
      channel & voice();
      channel & gui();

      INTERNAL::channelImplementation * addChannelImplementation(const String & name);
      void removeChannelImplementation(INTERNAL::channelImplementation * ptr);

      channelManager();
      ~channelManager();
      juce_DeclareSingleton(channelManager, true)
    private:
      channel _mainMix;
      channel _fx;
      channel _music;
      channel _ambient;
      channel _voice;
      channel _gui;
      UInt numberOfOutputs; // nr of output channels

      void setMono();
      void setStereo();
      void setQuad();
      void set51();
      void set51Side();
      void set61();
      void set71();
      void setAuto(Int count);

      std::forward_list<INTERNAL::channelImplementation> implementations;
    };

  }
}



#endif  // CHANNELMANAGER_H_INCLUDED
