#pragma once
#include "../pObject.h"

namespace YSE {
	namespace PATCHER {
		PATCHER_CLASS(mMidiProgramChange, YSE::OBJ::M_PROGCHANGE)
			_NO_MESSAGES
			_DO_CALCULATE

			_INT_IN(SetIntValue)

		private:
			int cvalue;
			int channel;
		};
	}
}