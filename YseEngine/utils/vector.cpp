/*
  ==============================================================================

    vector.cpp
    Created: 29 Jan 2014 11:12:33pm
    Author:  yvan

  ==============================================================================
*/

#include "vector.hpp"
#include "../utils/atomicPos.h"

YSE::Pos::Pos(const aPos& v) {
  v.loadInto(x, y, z);
}

YSE::aPos::aPos(const Pos& v) {
  store(v.x, v.y, v.z);
}

YSE::aPos& YSE::aPos::operator=(const Pos& v) {
  store(v.x, v.y, v.z);
  return *this;
}

void YSE::aPos::store(const Pos& v) {
  store(v.x, v.y, v.z);
}

YSE::Pos YSE::aPos::load() const {
  Flt x, y, z;
  loadInto(x, y, z);
  return Pos(x, y, z);
}