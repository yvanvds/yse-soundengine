#include "headers/defines.hpp"
#if YSE_WINDOWS
#include "mMidiNoteOff.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className mMidiNoteOff

CONSTRUCT() {
	ADD_IN_0;
	REG_INT_IN(SetIntPitch);

	ADD_IN_1;
	REG_INT_IN(SetIntVelocity);

	ADD_OUT_LIST;

	ADD_PARAM(channel);

	channel = pitch = velocity = 0;
}

INT_IN(SetIntPitch) {
	pitch = (int)value;
	if (pitch < 0) pitch = 0;
	if (pitch > 127) pitch = 127;
}

INT_IN(SetIntVelocity) {
	velocity = (int)value;
	if (velocity < 0) velocity = 0;
	if (velocity > 127) velocity = 127;
}

CALC() {
	std::string message = "000";
	message[0] = 0x80 + channel;
	message[1] = pitch;
	message[2] = velocity;
	outputs[0].SendList(message, thread);
}
#endif