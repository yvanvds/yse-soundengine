#pragma once
#include "utils/vector.hpp"

namespace YSE {

  class API listener {
  public:

		Vec  pos(); Vec vel(); Vec fwd(); Vec up();
		listener& pos(const Vec &pos);
		listener& orn(const Vec &forward	, const Vec &up		= Vec(0,1,0));	

  };

  extern API listener Listener;
}