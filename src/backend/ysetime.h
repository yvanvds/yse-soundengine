#pragma once

namespace YSE {

	struct time {
		ULong current;
		ULong last;
		Flt delta;
		time() { current = last = 0; delta = 0.0f; }
		void update();
	};

	extern time Time;
}