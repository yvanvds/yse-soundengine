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

