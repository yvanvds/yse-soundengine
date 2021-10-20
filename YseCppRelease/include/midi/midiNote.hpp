#pragma once
#include "../headers/defines.hpp"
#include "midiMessage.hpp"

namespace YSE {
	namespace MIDI {

		class API midiNote : public midiMessage {
		public:
			midiNote(unsigned char note, unsigned velocity);

			void note(unsigned char note);
			unsigned char note() const;

			void velocity(unsigned char velocity);
			unsigned char velocity() const;
		};

	}
}