#include "stdafx.h"
#include "listenerimpl.h"
#include "backend/ysetime.h"
#include "internal/settings.h"

YSE::listenerImpl YSE::ListenerImpl;

YSE::listenerImpl::listenerImpl() {
	_newPos.zero();
	_lastPos.zero();
	_vel = Vec(0);
	_forward = Vec(0,0,1);
	_up = Vec(0,1,0);
  _pos = Vec(0);
}

void YSE::listenerImpl::update() {
	_newPos = _pos * Settings.distanceFactor;
	_vel = (_newPos - _lastPos) * (1 / Time.delta);
	_lastPos = _newPos;
}
