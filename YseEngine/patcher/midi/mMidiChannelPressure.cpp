#include "headers/defines.hpp"
#if YSE_WINDOWS
#include "mMidiChannelPressure.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className mMidiChannelPressure

CONSTRUCT() {
	ADD_IN_0;
	REG_INT_IN(SetIntValue);

	ADD_OUT_LIST;

	ADD_PARAM(channel);

	channel = cvalue = 0;

	ADD_DESCRIPTION("MIDI Channel Pressure (aftertouch) message generator. Emits a status/value MIDI packet on every value change.");
	ADD_CATEGORY(pCategory::MIDI);
	INLET_DOC(0, "pressure", "Aftertouch pressure value (also fires the output).", "0-127");
	OUTLET_DOC(0, "midi", "Encoded MIDI Channel Pressure message.", "");
	PARAM_DOC("channel", "0", "MIDI channel offset (0-based).", "0-15");
}

INT_IN(SetIntValue) {
	cvalue = (int)value;
	if (cvalue < 0) cvalue = 0;
	if (cvalue > 127) cvalue = 127;
}

CALC() {
	std::string message = "000";
	message[0] = 0xD0 + channel;
	message[1] = cvalue;
	message[2] = 0;
	outputs[0].SendList(message, thread);
}
#endif