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

YSE::Vec YSE::listener::getPosition() {
  return INTERNAL::Global.getListener().pos;
}

YSE::Vec YSE::listener::getVelocity() {
  return INTERNAL::Global.getListener().vel;
}

YSE::Vec YSE::listener::getForward() {
  return INTERNAL::Global.getListener().forward;
}

YSE::Vec YSE::listener::getUpward() {
  return INTERNAL::Global.getListener().up;
}

YSE::listener& YSE::listener::setPosition(const Vec &pos) {
  INTERNAL::Global.getListener().pos.x = pos.x;
  INTERNAL::Global.getListener().pos.y = pos.y;
  INTERNAL::Global.getListener().pos.z = pos.z;
  return (*this);
}

YSE::listener& YSE::listener::setOrientation(const Vec &forward, const Vec &up) {
  INTERNAL::Global.getListener().forward = forward;
  INTERNAL::Global.getListener().up = up;
  return (*this);
}