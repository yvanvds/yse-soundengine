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


  class API deviceSetup {
  public:
    deviceSetup();

    deviceSetup & setInput(const device & in);
    deviceSetup & setOutput(const device & out);
    deviceSetup & setSampleRate(double value);
    deviceSetup & setBufferSize(int value);
    int getOutputChannels() const;

  private:
    const device *  in;
    const device * out;
    double sampleRate;
    int bufferSize;

    friend class YSE::DEVICE::managerObject;
  };

}



#endif  // DEVICEIMPLEMENTATION_H_INCLUDED
