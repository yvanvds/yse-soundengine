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
#include "JuceHeader.h"
#include "../headers/types.hpp"
#include "../reverb/reverbManager.h"

namespace YSE {
  namespace INTERNAL {

    class global {
    public:
      bool isActive() { return active; }
      deviceManager  & getDeviceManager();
      soundManager   & getSoundManager();
      channelManager & getChannelManager();
      REVERB::managerObject & getReverbManager();

      logImplementation & getLog();
      time              & getTime();
      settings          & getSettings();

      listenerImplementation & getListener();

      void addSlowJob(ThreadPoolJob * job);
      void addFastJob(ThreadPoolJob * job);
      void waitForSlowJob(ThreadPoolJob * job);
      void waitForFastJob(ThreadPoolJob * job);
      bool containsSlowJob(ThreadPoolJob * job);
      
      void flagForUpdate() { update++;  }
      bool needsUpdate() { return update > 0;  }
      void updateDone() { update--; }

      global();

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
      REVERB::managerObject * rm;

      ThreadPool slowThreads;
      ThreadPool fastThreads;

      aInt update;
      aBool active; // set true after System().init(), false at System().close()

      friend class system; // system needs access to the init and close method
    };

    extern global Global;
  }
}



#endif  // GLOBAL_H_INCLUDED
