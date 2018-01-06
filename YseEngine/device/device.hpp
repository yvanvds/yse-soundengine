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

  class device;
  class deviceSetup;

  namespace DEVICE {
    class managerObject;   
    class deviceManager;

	  managerObject & Manager();
  }

}



#endif  // DEVICE_H_INCLUDED
