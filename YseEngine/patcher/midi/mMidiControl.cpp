#include "headers/defines.hpp"
#if YSE_WINDOWS
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

	ADD_DESCRIPTION("MIDI Control Change message generator. Emits a 3-byte status/controller/value packet on every value change.");
	ADD_CATEGORY(pCategory::MIDI);
	INLET_DOC(0, "value", "Controller value (also fires the output).", "0-127");
	OUTLET_DOC(0, "midi", "Encoded MIDI Control Change message.", "");
	PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
	PARAM_DOC("controller", "0", "Controller number.", "0-127");
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
#endif