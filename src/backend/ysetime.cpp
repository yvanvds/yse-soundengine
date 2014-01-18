#include "stdafx.h"
#include "ysetime.h"
#include <ctime>

YSE::time YSE::Time;

void YSE::time::update() {
	// update time delta
	last = current;
	current = clock();
	delta = (current - last) / (Flt)CLOCKS_PER_SEC;
}
