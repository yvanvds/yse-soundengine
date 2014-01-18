#include "stdafx.h"
#include "listener.hpp"
#include "internal/listenerimpl.h"

YSE::listener YSE::Listener;

YSE::Vec YSE::listener::pos() {
  return ListenerImpl._pos;
}

YSE::Vec YSE::listener::vel() {
  return ListenerImpl._vel;
}

YSE::Vec YSE::listener::fwd() {
  return ListenerImpl._forward;
}

YSE::Vec YSE::listener::up() {
  return ListenerImpl._up;
}

YSE::listener& YSE::listener::pos(const Vec &pos) {
  ListenerImpl._pos = pos;
  return (*this);
}

YSE::listener& YSE::listener::orn(const Vec &forward, const Vec &up) {
  ListenerImpl._forward = forward;
  ListenerImpl._up = up;
  return (*this);
}