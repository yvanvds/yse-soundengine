#pragma once
#include <atomic>
#include "../headers/types.hpp"

namespace YSE {
	class Pos;


	class API aPos {
	public:
		std::atomic<Flt> x;
		std::atomic<Flt> y;
		std::atomic<Flt> z;

		aPos() { x.store(0.f), y.store(0.f), z.store(0.f); }
		aPos(Flt x, Flt y, Flt z) {
			this->x.store(x);
			this->y.store(y);
			this->z.store(z);
		}
		aPos(const Pos & v) { x.store(v.x); y.store(v.y); z.store(v.z); }
		aPos & operator=(const Pos & v) { x.store(v.x); y.store(v.y); z.store(v.z); return *this; }
	};
}
