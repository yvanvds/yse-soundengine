/*
  ==============================================================================

    listener.cpp
    Created: 30 Jan 2014 4:22:19pm
    Author:  yvan

  ==============================================================================
*/

#include "internalHeaders.h"

YSE::listener& YSE::Listener() {
  static listener l;
  return l;
}

YSE::Pos YSE::listener::pos() {
  return INTERNAL::ListenerImpl().pos;
}

YSE::Pos YSE::listener::vel() {
  return INTERNAL::ListenerImpl().vel;
}

YSE::Pos YSE::listener::forward() {
  return INTERNAL::ListenerImpl().forward;
}

YSE::Pos YSE::listener::upward() {
  return INTERNAL::ListenerImpl().up;
}

YSE::listener& YSE::listener::pos(const Pos& pos) {
  INTERNAL::ListenerImpl().pos.store(pos);
  return (*this);
}

YSE::listener& YSE::listener::orient(const Pos& forward, const Pos& up) {
  INTERNAL::ListenerImpl().forward.store(forward);
  INTERNAL::ListenerImpl().up.store(up);
  return (*this);
}