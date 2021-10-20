#include "headers/defines.hpp"
#if YSE_WINDOWS
#include "mMidiOut.h"
#include "../pObjectList.hpp"
#include "../patcherImplementation.h"

using namespace YSE::PATCHER;
#define className mMidiOut

CONSTRUCT(), ready(false) {
	ADD_IN_0;
	REG_LIST_IN(SetListValue);

	ADD_PARAM(port);
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