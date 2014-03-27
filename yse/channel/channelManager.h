/*
  ==============================================================================

    channelManager.h
    Created: 1 Feb 2014 2:43:30pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CHANNELMANAGER_H_INCLUDED
#define CHANNELMANAGER_H_INCLUDED

#include <forward_list>
#include "JuceHeader.h"
#include "channel.hpp"
#include "../templates/managerObject.h"
#include "../headers/enums.hpp"
#include "channelInterface.hpp"
#include "channelImplementation.h"
#include "channelMessage.h"


namespace YSE {
  namespace CHANNEL {

    class managerObject : public TEMPLATE::managerObject<channelSubSystem> {
    public:
      managerObject();
      ~managerObject();

      void create() {};
      void update();
      
      // channel output configuration
      void changeChannelConf(CHANNEL_TYPE type, Int outputs = 2);
      UInt getNumberOfOutputs();
      Flt  getOutputAngle(UInt nr);

      channel & master();
      channel & FX();
      channel & music();
      channel & ambient();
      channel & voice();
      channel & gui();

      void setMaster(implementationObject * impl);
      

      juce_DeclareSingleton(managerObject, true)
    private:

      channel _master;
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

    };

  }
}



#endif  // CHANNELMANAGER_H_INCLUDED
