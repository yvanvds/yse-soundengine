/*
  ==============================================================================

    vector.cpp
    Created: 29 Jan 2014 11:12:33pm
    Author:  yvan

  ==============================================================================
*/

#include "vector.hpp"
#include "../utils/atomicPos.h"

YSE::Pos::Pos(const aPos & v) { set(v.x.load(), v.y.load(), v.z.load()); }