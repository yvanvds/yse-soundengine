#pragma once
#if YSE_WINDOWS

#include "../dependencies/rtmidi/include/RtMidi.h"
#include "../midi/midiMessage.hpp"
#include <map>

namespace YSE {
	namespace MIDI {

		class deviceManager {
		public:
			deviceManager();
			~deviceManager();

			unsigned int getNumMidiInDevices();
			unsigned int getNumMidiOutDevices();

			const std::string getMidiInDeviceName(unsigned int ID);
			const std::string getMidiOutDeviceName(unsigned int ID);

			RtMidiOut * getMidiOutPort(unsigned int ID);

		private:
			bool isPrepared();

			RtMidiIn* midiIn;
			RtMidiOut* midiOut;
			bool initialized;

			std::map<unsigned int, RtMidiOut*> midiOutPorts;
		};

		deviceManager& DeviceManager();

		void GenerateMidiError(const RtMidiError & error);
	}
}

#endif