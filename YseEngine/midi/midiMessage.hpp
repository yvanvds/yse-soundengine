#pragma once
#include "../headers/defines.hpp"

namespace YSE {
	namespace MIDI {

		class API midiMessage {
		public:
			inline const std::vector<unsigned char> * getRaw() const {
				return &raw;
			}

		protected:
			std::vector<unsigned char> raw;
		};



	}
}