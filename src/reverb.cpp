#include "stdafx.h"
#include "reverb.hpp"
#include "internal/reverbimpl.h"
#include "internal/internalObjects.h"
#include "utils/error.hpp" 

YSE::reverb YSE::GlobalReverb;

YSE::reverb::reverb() {
  pimpl = NULL;
}

YSE::reverb& YSE::reverb::release() {
  if (pimpl) {
    pimpl->_release = true;
    pimpl->link = NULL;
  }
  pimpl = NULL;
  return *this;
}

YSE::reverb::~reverb() {
  release();
}

YSE::reverb& YSE::reverb::create() {
  //lock l(MTX);
	
  Reverbs().push_back(new reverbimpl);
  pimpl = &Reverbs().back();
  pimpl->link = &pimpl;
	
  return *this;
}

YSE::reverb& YSE::reverb::pos(const Vec &value) {
  if (pimpl) pimpl->pos(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

YSE::Vec YSE::reverb::pos() {
  if (pimpl) return pimpl->pos();
  else Error.emit(E_REVERB_NO_INIT);
  return Vec(0);
}

YSE::reverb& YSE::reverb::size(Flt value) {
  if (pimpl) pimpl->size(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::size() {
  if (pimpl) return pimpl->size();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::rolloff(Flt value) {
  if (pimpl) pimpl->rolloff(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::rolloff() {
  if (pimpl) return pimpl->rolloff();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::active(Bool value) {
  if (pimpl) pimpl->active(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Bool YSE::reverb::active() {
  if (pimpl) return pimpl->active();
  else Error.emit(E_REVERB_NO_INIT);
  return false;
}

YSE::reverb& YSE::reverb::roomsize(Flt value) {
  if (pimpl) pimpl->roomsize(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::roomsize() {
  if (pimpl) return pimpl->roomsize();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::damp(Flt value) {
  if (pimpl) pimpl->damp(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::damp() {
  if (pimpl) return pimpl->damp();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::wet(Flt value) {
  if (pimpl) pimpl->wet(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::wet() {
  if (pimpl) return pimpl->wet();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::dry(Flt value) {
  if (pimpl) pimpl->dry(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::dry() {
  if (pimpl) return pimpl->dry();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::modFreq(Flt value) {
  if (pimpl) pimpl->modFreq(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::modFreq() {
  if (pimpl) return pimpl->modFreq();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::modWidth(Flt value) {
  if (pimpl) pimpl->modWidth(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::modWidth() {
  if (pimpl) return pimpl->modWidth();
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::reflectionTime(Int reflection, Int value) {
  if (pimpl) pimpl->reflectionTime(reflection, value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Int YSE::reverb::reflectionTime(Int reflection) {
  if (pimpl) return pimpl->reflectionTime(reflection);
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::reflectionGain(Int reflection, Flt value) {
  if (pimpl) pimpl->reflectionGain(reflection, value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}

Flt YSE::reverb::reflectionGain(Int reflection) {
  if (pimpl) return pimpl->reflectionGain(reflection);
  else Error.emit(E_REVERB_NO_INIT);
  return 0;
}

YSE::reverb& YSE::reverb::preset(REVERB_PRESET value) {
  if (pimpl) pimpl->preset(value);
  else Error.emit(E_REVERB_NO_INIT);
  return *this;
}
