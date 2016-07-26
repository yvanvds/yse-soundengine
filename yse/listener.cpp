/*
  ==============================================================================

    listener.cpp
    Created: 30 Jan 2014 4:22:19pm
    Author:  yvan

  ==============================================================================
*/

#include "internalHeaders.h"

YSE::listener & YSE::Listener() {
  static listener l;
  return l;
}

YSE::Pos YSE::listener::getPosition() {
  return INTERNAL::ListenerImpl().pos;
}

YSE::Pos YSE::listener::getVelocity() {
  return INTERNAL::ListenerImpl().vel;
}

YSE::Pos YSE::listener::getForward() {
  return INTERNAL::ListenerImpl().forward;
}

YSE::Pos YSE::listener::getUpward() {
  return INTERNAL::ListenerImpl().up;
}

YSE::listener& YSE::listener::setPosition(const Pos &pos) {
  INTERNAL::ListenerImpl().pos.x = pos.x;
  INTERNAL::ListenerImpl().pos.y = pos.y;
  INTERNAL::ListenerImpl().pos.z = pos.z;
  return (*this);
}

YSE::listener& YSE::listener::setOrientation(const Pos &forward, const Pos &up) {
  INTERNAL::ListenerImpl().forward = forward;
  INTERNAL::ListenerImpl().up = up;
  return (*this);
}