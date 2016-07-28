/*
  ==============================================================================

    abstractDeviceManager.h
    Created: 27 Jul 2016 1:02:03pm
    Author:  yvan

  ==============================================================================
*/

#ifndef ABSTRACTDEVICEMANAGER_H_INCLUDED
#define ABSTRACTDEVICEMANAGER_H_INCLUDED

#include "../classes.hpp"
#include "../headers/types.hpp"
#include <vector>

namespace YSE {

  namespace CHANNEL {
    class implementationObject;
  }

  namespace DEVICE {

    class abstractDeviceManager {
    public:
      /* you should call the updateDeviceList in the constructor */
      abstractDeviceManager();
      virtual ~abstractDeviceManager();

      virtual Bool init ();
      virtual void close() {};

      /* If the audio backend provides a method to retrieve
         the cpu load, use it. Otherwise just return a number.
         YSE does not depend on this method, but it can be useful
         for testing.
      */
      virtual Flt cpuLoad() = 0;
      
      /* this method should populate the devices vector.
      */
      virtual void updateDeviceList() {};

      virtual void openDevice(const YSE::DEVICE::setupObject & object) = 0;
      virtual void addCallback() = 0;

      bool doOnCallback(int numSamples);

      void setMaster(CHANNEL::implementationObject * ptr);
      CHANNEL::implementationObject & getMaster();

      
      const std::vector<interfaceObject> & getDeviceList();

      const std::string & getDefaultTypeName();
      const std::string & getDefaultDeviceName();

    protected:
      std::vector<interfaceObject> devices;
      std::string defaultTypeName;
      std::string defaultDeviceName;

      CHANNEL::implementationObject * master;
      int currentInputChannels, currentOutputChannels;

    };

  }

}



#endif  // ABSTRACTDEVICEMANAGER_H_INCLUDED
