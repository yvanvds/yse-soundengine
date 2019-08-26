#pragma once
#include "../pObject.h"
#include "midi/device.hpp"

namespace YSE {
	namespace PATCHER {

		PATCHER_CLASS(mMidiOut, YSE::OBJ::M_OUT)
			_DO_MESSAGES
			_NO_CALCULATE

			_LIST_IN(SetListValue)

		private:
			aInt port;
			YSE::midiOut out;
			bool ready;
		};
	}
}