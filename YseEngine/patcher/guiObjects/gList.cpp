#include "gList.h"
#include "../pObjectList.hpp"

using namespace YSE::PATCHER;

#define className gList

CONSTRUCT() {
	ADD_IN_0;
	REG_BANG_IN(Bang);
	REG_LIST_IN(SetValue);

	ADD_OUT_LIST;

	ADD_PARAM(message);
}

BANG_IN(Bang) {
	outputs[0].SendList(message, thread);
}

LIST_IN(SetValue) {
	message = value;
}

GUI_VALUE() {
	return message;
}

MESSAGES() {
	this->message = message;
}