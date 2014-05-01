/*
  ==============================================================================

    device.h
    Created: 10 Apr 2014 6:05:17pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DEVICE_H_INCLUDED
#define DEVICE_H_INCLUDED

namespace YSE {
  /** Every subSystem consists out of several class which are meant to work together.
  They all have an interface, implementation, manager, message and a message enumeration.
  (Device does not have the message part, because it is not needed.)
  */
  namespace DEVICE {
    class interfaceObject;
    class setupObject;
    class managerObject;   
  }

  // the interface itself gets a more generic name, so that users can just
  // define a 'device' to get an interface object.
  typedef DEVICE::interfaceObject device;
  typedef DEVICE::setupObject deviceSetup;
}



#endif  // DEVICE_H_INCLUDED
