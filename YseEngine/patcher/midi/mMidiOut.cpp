#include "headers/defines.hpp"
// See the matching guard in mMidiOut.h.
#if YSE_WINDOWS && YSE_ENABLE_MIDI_DEVICE
#include "mMidiOut.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;
#define className mMidiOut

CONSTRUCT(), ready(false) {
	ADD_IN_0;
	REG_LIST_IN(SetListValue);

	ADD_PARAM(port);

	ADD_DESCRIPTION("MIDI device output. Sends raw MIDI byte lists to the selected hardware port; understands 'allnotesoff', 'reset', 'omni on/off', 'poly on/off', and 'local control on/off' as control messages.");
	ADD_CATEGORY(pCategory::MIDI);
	INLET_DOC(0, "midi", "Raw MIDI byte list to send to the device.", "");
	PARAM_DOC("port", "0", "Index of the output MIDI port.", "device-dependent");
}

LIST_IN(SetListValue) {
	if (!ready) {
		out.create(port);
		ready = true;
	}
	out.Raw(value);
}

MESSAGES() {
	if (message == "allnotesoff") {
		out.AllNotesOff();
	}
	else if (message == "reset") {
		out.Reset();
	}
	else if (message == "omni on") {
		out.Omni(true);
	}
	else if (message == "omni off") {
		out.Omni(false);
	}
	else if (message == "poly on") {
		out.Poly(true);
	}
	else if (message == "poly off") {
		out.Poly(false);
	}
	else if (message == "local control on") {
		out.LocalControl(true);
	}
	else if (message == "local control off") {
		out.LocalControl(false);
	}
}
#endif