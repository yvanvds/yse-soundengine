#if YSE_WINDOWS
#pragma once
#include "../pObject.h"

namespace YSE {
	namespace PATCHER {
		PATCHER_CLASS(mMidiPolyPressure, YSE::OBJ::M_CONTROL)
			_NO_MESSAGES
			_DO_CALCULATE

			_INT_IN(SetIntPitch)
			_INT_IN(SetIntPressure)

		private:
			int pitch;
			int pressure;
			int channel;
		};
	}
}

#endif