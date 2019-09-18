#include "mMidiControl.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className mMidiControl

CONSTRUCT() {
	ADD_IN_0;
	REG_INT_IN(SetIntValue);

	ADD_OUT_LIST;

	ADD_PARAM(channel);
	ADD_PARAM(controller);

	channel = controller = cvalue = 0;
}

INT_IN(SetIntValue) {
	cvalue = (int)value;
	if (cvalue < 0) cvalue = 0;
	if (cvalue > 127) cvalue = 127;
}


CALC() {
	std::string message = "000";
	message[0] = 0xB0 + channel;
	message[1] = controller;
	message[2] = cvalue;
	outputs[0].SendList(message, thread);
}