#pragma once

namespace YSE {

	struct settings {
		Flt dopplerScale;
		Flt distanceFactor;
		Flt rolloffScale;

		settings();
	};

	extern settings Settings;
}