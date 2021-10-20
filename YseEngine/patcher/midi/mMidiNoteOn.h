#if YSE_WINDOWS
#pragma once
#include "../pObject.h"

namespace YSE {
	namespace PATCHER {
		PATCHER_CLASS(mMidiNoteOn, YSE::OBJ::M_NOTEON)
			_NO_MESSAGES
			_DO_CALCULATE

			_INT_IN(SetIntPitch)
			_INT_IN(SetIntVelocity)


		private:
			int pitch;
			int velocity;
			int channel;
		};
	}
}

#endif