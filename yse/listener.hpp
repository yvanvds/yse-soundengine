/*
  ==============================================================================

    listener.h
    Created: 30 Jan 2014 4:22:19pm
    Author:  yvan

  ==============================================================================
*/

#include "utils/vector.hpp"

#ifndef LISTENER_H_INCLUDED
#define LISTENER_H_INCLUDED

namespace YSE {

  class API listener {
  public:

    Vec  pos(); Vec vel(); Vec fwd(); Vec up();
    listener& pos(const Vec &pos);
    listener& orn(const Vec &forward, const Vec &up = Vec(0, 1, 0));

  };

  listener & Listener();
}



#endif  // LISTENER_H_INCLUDED
