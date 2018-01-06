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


/** This class can hold the properties of a particular audio device. You're not
    supposed to create object of this class yourself, but you can retrieve the
    available audio devices from the System() object.
*/
namespace YSE {


  class API device {
  public:
    /** Don't create this object yourself! Instead, retrieve
        available audio devices with YSE::System().getDevices()
    */
    device();

    device & setName(const std::string & name);

    /** Get the name of this device
    */
    const std::string & getName() const;

    device & setTypeName(const std::string & name);

    /** Get the type of the device. This is also known as the device host.
        A system can have different hosts, like ASIO, Jack, etc.
    */
    const std::string & getTypeName() const;

    device & addInputChannelName(const std::string & name);
    device & addOutputChannelName(const std::string & name);
    device & addAvailableSampleRate(double sr);
    device & addAvailableBufferSize(int bs);

    // These functions cannot be used if YSE is compiled as DLL, because
    // you can't pass a vector if this is the case.
    const std::vector<std::string> & getOutputChannelNames() const;
    const std::vector<std::string> & getInputChannelNames() const;
    const std::vector<double> & getAvailableSampleRates() const;
    const std::vector<int> & getAvailableBufferSizes() const;

    // use these instead.
    UInt getNumOutputChannelNames() const;
    const std::string & getOutputChannelName(UInt nr) const;

    UInt getNumInputChannelNames() const;
    const std::string & getInputChannelName(UInt nr) const;

    UInt getNumAvailableSampleRates() const;
    double getAvailableSampleRate(UInt nr) const;

    UInt getNumAvailableBufferSizes() const;
    Int getAvailableBufferSize(UInt nr) const;

    device & setDefaultBufferSize(int value);
    int getDefaultBufferSize() const;

    device & setOutputLatency(int value);
    int getOutputLatency() const;

    device & setInputLatency(int value);
    int getInputLatency() const;

    device & setID(int value);
    int getID() const;

  private:
    std::string name;
    std::string typeName;

    std::vector<std::string> outputChannelNames;
    std::vector<std::string> inputChannelNames;
    std::vector<double> sampleRates;
    std::vector<int> bufferSizes;

    int defaultBufferSize;
    int inputLatency, outputLatency;
    int ID;

    friend class DEVICE::managerObject;
  };



}



#endif  // DEVICE_HPP_INCLUDED
