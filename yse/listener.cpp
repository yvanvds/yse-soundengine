/*
  ==============================================================================

    listener.cpp
    Created: 30 Jan 2014 4:22:19pm
    Author:  yvan

  ==============================================================================
*/

#include "listener.hpp"
#include "internal/global.h"
#include "implementations/listenerImplementation.h"

YSE::listener & YSE::Listener() {
  static listener l;
  return l;
}

YSE::Vec YSE::listener::pos() {
  return INTERNAL::Global.getListener().pos;
}

YSE::Vec YSE::listener::vel() {
  return INTERNAL::Global.getListener().vel;
}

YSE::Vec YSE::listener::fwd() {
  return INTERNAL::Global.getListener().forward;
}

YSE::Vec YSE::listener::up() {
  return INTERNAL::Global.getListener().up;
}

YSE::listener& YSE::listener::pos(const Vec &pos) {
  INTERNAL::Global.getListener().pos = pos;
  return (*this);
}

YSE::listener& YSE::listener::orn(const Vec &forward, const Vec &up) {
  INTERNAL::Global.getListener().forward = forward;
  INTERNAL::Global.getListener().up = up;
  return (*this);
}