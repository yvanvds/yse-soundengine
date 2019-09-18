#pragma once
#include "../pObject.h"

namespace YSE {
	namespace PATCHER {
		PATCHER_CLASS(mMidiNoteOff, YSE::OBJ::M_NOTEOFF)
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