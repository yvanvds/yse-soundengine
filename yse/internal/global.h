/*
  ==============================================================================

    global.h
    Created: 27 Jan 2014 10:18:27pm
    Author:  yvan

  ==============================================================================
*/

#ifndef GLOBAL_H_INCLUDED
#define GLOBAL_H_INCLUDED
#include "../classes.hpp"

namespace YSE {
  namespace INTERNAL {

    class global {
    public:
      deviceManager  & getDeviceManager();
      soundManager   & getSoundManager();
      channelManager & getChannelManager();
      reverbManager  & getReverbManager();

      logImplementation & getLog();
      time              & getTime();
      settings          & getSettings();

      listenerImplementation & getListener();

    private:
      void init();
      void close();

      deviceManager * dm;
      soundManager  * sm;
      logImplementation * log;
      time * ysetime;
      settings * set;
      channelManager * cm;
      listenerImplementation * li;
      reverbManager * rm;

      friend class system; // system needs access to the init and close method
    };

    extern global Global;
  }
}



#endif  // GLOBAL_H_INCLUDED
