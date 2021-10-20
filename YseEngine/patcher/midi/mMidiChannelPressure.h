#if YSE_WINDOWS
#pragma once
#include "../pObject.h"

namespace YSE {
	namespace PATCHER {
		PATCHER_CLASS(mMidiChannelPressure, YSE::OBJ::M_CHANPRESS)
			_NO_MESSAGES
			_DO_CALCULATE

			_INT_IN(SetIntValue)

		private:
			int cvalue;
			int channel;
		};
	}
}
#endif