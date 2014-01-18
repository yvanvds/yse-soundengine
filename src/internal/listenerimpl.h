#pragma once
#include <atomic>
#include "utils/vector.hpp"
#include "utils/guard.h"

namespace YSE {

	class listenerImpl {
  public:
		void update();
		listenerImpl();

    Vec _newPos, _lastPos;
    aVec _pos;
    aVec _up;
    aVec _forward;
    aVec _vel;
	};

	extern listenerImpl ListenerImpl;
}
