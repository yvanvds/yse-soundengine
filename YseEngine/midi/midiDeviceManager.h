#pragma once
#include "headers/defines.hpp"
#if YSE_ENABLE_MIDI_DEVICE

#include "../dependencies/rtmidi/include/RtMidi.h"
#include "../midi/midiMessage.hpp"
#include <map>
#include <memory>

namespace YSE {
  namespace MIDI {

    class deviceManager {
    public:
      deviceManager();
      ~deviceManager();

      deviceManager(const deviceManager&) = delete;
      deviceManager& operator=(const deviceManager&) = delete;
      deviceManager(deviceManager&&) = delete;
      deviceManager& operator=(deviceManager&&) = delete;

      unsigned int getNumMidiInDevices();
      unsigned int getNumMidiOutDevices();

      const std::string getMidiInDeviceName(unsigned int ID);
      const std::string getMidiOutDeviceName(unsigned int ID);

      RtMidiOut* getMidiOutPort(unsigned int ID);

    private:
      bool isPrepared();

      std::unique_ptr<RtMidiIn> midiIn;
      std::unique_ptr<RtMidiOut> midiOut;
      bool initialized = false;

      std::map<unsigned int, std::unique_ptr<RtMidiOut>> midiOutPorts;
    };

    deviceManager& DeviceManager();

    void GenerateMidiError(const RtMidiError& error);
  } // namespace MIDI
} // namespace YSE

#endif