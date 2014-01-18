#include "stdafx.h"
#include "playlist.hpp"
#include "sound.hpp"
#include "internal/soundimpl.h" // only for SOUND_STATUS
#include "system.hpp"
#include "utils/misc.hpp"
#include "internal/playlistimpl.h"
#include "internal/internalObjects.h"

YSE::playlist::playlist() {
  Playlists().push_back(new playlistImpl);
  pimpl = &Playlists().back();
  pimpl->link = &pimpl;
}

YSE::playlist::~playlist() {
  for (boost::ptr_list<playlistImpl>::iterator i = Playlists().begin(); i != Playlists().end(); ++i) {
    if (&*i == pimpl) {
      pimpl->link = NULL;
      Playlists().release(i);
      return;
    }
  }
}


YSE::playlist& YSE::playlist::add(const char *  filename) {
  pimpl->list.push_back(filename);
  pimpl->sounds.push_back(new sound);
  pimpl->sounds.back().create(pimpl->list.back().c_str(), pimpl->ch, false, pimpl->volume, pimpl->stream);
  if (pimpl->sounds.back().valid()) {
    pimpl->sounds.back().pos(pimpl->pos);
    pimpl->sounds.back().spread3D(pimpl->spread3D);
    pimpl->sounds.back().volume(0);
    pimpl->sounds.back().speed(pimpl->speed);
    pimpl->sounds.back().size(pimpl->size);
    pimpl->sounds.back().relative(pimpl->relative);
    pimpl->sounds.back().doppler(pimpl->doppler);
    pimpl->sounds.back().pan2D(pimpl->pan2D);
  }
  return (*this);
}

void YSE::playlist::clear() {
  pimpl->list.clear();
  pimpl->sounds.clear();
}

Int YSE::playlist::elms() {
  return pimpl->list.size();
}

Int YSE::playlist::current() {
  return pimpl->currentFile;
}

YSE::playlist& YSE::playlist::pos(const Vec &v) {
  pimpl->pos = v;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].pos(v);
  }
  return (*this);
}

YSE::playlist& YSE::playlist::spread3D(Flt value) {
  pimpl->spread3D = value;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].spread3D(value);
  }
  return *this;
}

YSE::playlist& YSE::playlist::volume(Flt value) {
  pimpl->volume = value;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].volume(value);
  }
  return *this;
}

YSE::playlist& YSE::playlist::speed(Flt value) {
  pimpl->speed = value;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].speed(value);
  }
  return *this;
}

YSE::playlist& YSE::playlist::size(Flt value) {
  pimpl->size = value;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].size(value);
  }
  return *this;
}

YSE::playlist& YSE::playlist::play(Int nr) {
  pimpl->play(nr);
  return *this;
}


YSE::playlist& YSE::playlist::pause() {
  if (pimpl->sounds.empty()) return *this;
  pimpl->sounds[pimpl->currentFile].pause();
  pimpl->sounds[pimpl->nextFile].pause();
  pimpl->status = SS_PAUSED;
  return *this;
}

YSE::playlist& YSE::playlist::stop() {
  if (pimpl->sounds.empty()) return *this;
  pimpl->sounds[pimpl->currentFile].stop();
  pimpl->sounds[pimpl->nextFile].stop();
  pimpl->status = SS_STOPPED;
  return *this;
}

Bool YSE::playlist::playing() {
  if (pimpl->status == SS_PLAYING) return true;
  return false;
}

Bool YSE::playlist::paused() {
  if (pimpl->status == SS_PAUSED) return true;
  return false;
}

Bool YSE::playlist::stopped() {
  if (pimpl->status == SS_STOPPED) return true;
  return false;
}

Flt YSE::playlist::time() {
  if (pimpl->sounds.empty()) return 0;
  return pimpl->sounds[pimpl->currentFile].time();
}

Flt YSE::playlist::length() {
  if (pimpl->sounds.empty()) return 0;
  return pimpl->sounds[pimpl->currentFile].length();
  return 0;
}

YSE::playlist& YSE::playlist::relative(Bool value) {
  pimpl->relative = value;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].relative(value);
  }
  return *this;
}

YSE::playlist& YSE::playlist::doppler(Bool value) {
  pimpl->doppler = value;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].doppler(value);
  }
  return *this;
}

YSE::playlist& YSE::playlist::pan2D(Bool value) {
  pimpl->pan2D = value;
  for (Int i = 0; i < pimpl->sounds.size(); i++) {
    pimpl->sounds[i].pan2D(value);
  }
  return *this;
}

Bool YSE::playlist::relative() {
  return pimpl->relative;
}

Bool YSE::playlist::doppler() {
  return pimpl->doppler;
}

Bool YSE::playlist::pan2D() {
  return pimpl->pan2D;
}

YSE::playlist& YSE::playlist::streamed(Bool value) {
  pimpl->stream = value;
  return *this;
}

Bool YSE::playlist::streamed() {
  return pimpl->stream;
}

YSE::playlist& YSE::playlist::autoloop(Bool value) {
  pimpl->autoloop = value;
  return *this;
}

Bool YSE::playlist::autoloop() {
  return pimpl->autoloop;
}

YSE::playlist& YSE::playlist::output(channel& ch) {
  pimpl->ch = &ch;
  return *this;
}

YSE::channel& YSE::playlist::output() {
  return *pimpl->ch;
}
