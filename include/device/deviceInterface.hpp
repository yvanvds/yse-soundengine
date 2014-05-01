/*
  ==============================================================================

    device.hpp
    Created: 10 Apr 2014 2:43:14pm
    Author:  yvan

  ==============================================================================
*/

#ifndef DEVICEINTERFACE_HPP_INCLUDED
#define DEVICEINTERFACE_HPP_INCLUDED
#include <string>
#include <vector>
#include "../classes.hpp"
#include "../yse.hpp"

// TODO: this should be removed at some point. We don't want any juce references in the public interface
namespace juce {
  class AudioIODevice;
}

namespace YSE {
  namespace DEVICE {

    class API interfaceObject {
    public:
      interfaceObject(juce::AudioIODevice * pimpl);

      const char * getName() const;
      const char * getTypeName() const;

      const std::vector<std::string> & getOutputChannelNames() const;
      const std::vector<std::string> & getInputChannelNames() const;
      const std::vector<double> & getAvailableSampleRates() const;
      const std::vector<int> & getAvailableBufferSizes() const;
      int getDefaultBufferSize() const;
      int getOutputLatency() const;
      int getInputLatency() const;

    private:
      std::vector<std::string> outputChannelNames;
      std::vector<std::string> inputChannelNames;
      std::vector<double> sampleRates;
      std::vector<int> bufferSizes;

      juce::AudioIODevice * pimpl;

      friend class DEVICE::managerObject;
    };
  }

  
}



#endif  // DEVICE_HPP_INCLUDED
