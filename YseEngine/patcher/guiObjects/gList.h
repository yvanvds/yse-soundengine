#pragma once
#pragma once
#include "../pObject.h"
#include <string>

namespace YSE {
	namespace PATCHER {

		PATCHER_CLASS(gList, YSE::OBJ::G_LIST)
			_DO_MESSAGES
			_NO_CALCULATE

			_BANG_IN(Bang)
			_LIST_IN(SetValue)

			_HAS_GUI
private:
		std::string message;
		};
	}
}
