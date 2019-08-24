#pragma once
#include "RtMidi.h"
#include "..//midi/midiMessage.hpp"

namespace YSE {
	namespace MIDIDEVICE {

		class managerObject {
		public:
			managerObject();
			~managerObject();

			unsigned int getNumMidiInDevices();
			unsigned int getNumMidiOutDevices();

			const std::string getMidiInDeviceName(unsigned int ID);
			const std::string getMidiOutDeviceName(unsigned int ID);

			bool openMidiOutPort(unsigned int ID);

			void sendMessage(const MIDI::midiMessage& message);

		private:
			bool isPrepared();

			RtMidiIn* midiIn;
			RtMidiOut* midiOut;
			bool initialized;

		};

		managerObject& Manager();

		void GenerateMidiError(const RtMidiError & error);
	}
}