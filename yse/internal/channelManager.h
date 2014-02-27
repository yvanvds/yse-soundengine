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
    class channelSetupJob : public ThreadPoolJob {
    public:
      channelSetupJob() : ThreadPoolJob("channelSetupJob") {}
      JobStatus runJob();
    };

    class channelDeleteJob : public ThreadPoolJob {
    public:
      channelDeleteJob() : ThreadPoolJob("channelDeleteJob") {}
      JobStatus runJob();
    };


    class channelManager {
    public:
      void update();
      
      // channel output configuration
      void changeChannelConf(CHANNEL_TYPE type, Int outputs = 2);
      UInt getNumberOfOutputs();
      Flt  getOutputAngle(UInt nr);

      channel & mainMix();
      channel & FX();
      channel & music();
      channel & ambient();
      channel & voice();
      channel & gui();

      channelImplementation * add(const String & name, channel * head);
      void setup(channelImplementation * impl);

      channelManager();
      ~channelManager();
      juce_DeclareSingleton(channelManager, true)
    private:
      channelSetupJob channelSetup;
      channelDeleteJob channelDelete;

      channel _mainMix;
      channel _fx;
      channel _music;
      channel _ambient;
      channel _voice;
      channel _gui;

      // channel output configuration
      aFlt * outputAngles;
      aUInt outputChannels;

      void setMono();
      void setStereo();
      void setQuad();
      void set51();
      void set51Side();
      void set61();
      void set71();
      void setAuto(Int count);

      /**
        Lists for channel implementation maintenance
      */
      std::forward_list<std::atomic<channelImplementation*>> toCreate;
      std::forward_list<channelImplementation*> inUse;
      std::forward_list<channelImplementation> implementations;
      aBool runDelete;

      friend class channelSetupJob;
      friend class channelDeleteJob;
    };

  }
}



#endif  // CHANNELMANAGER_H_INCLUDED
