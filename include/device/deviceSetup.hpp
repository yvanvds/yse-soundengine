/*
  ==============================================================================

    deviceImplementation.h
    Created: 10 Apr 2014 6:05:33pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DEVICESETUP_HPP_INCLUDED
#define DEVICESETUP_HPP_INCLUDED

#include "../classes.hpp"

namespace YSE {
  namespace DEVICE {

    class API setupObject  {
    public:
      setupObject();

      setupObject & setInput(const interfaceObject & in);
      setupObject & setOutput(const interfaceObject & out);
      setupObject & setSampleRate(double value);
      setupObject & setBufferSize(int value);
      int getOutputChannels() const;

    private:
      const interfaceObject *  in;
      const interfaceObject * out;
      double sampleRate;
      int bufferSize;

      friend class YSE::DEVICE::managerObject;
    };
  }
}



#endif  // DEVICEIMPLEMENTATION_H_INCLUDED
