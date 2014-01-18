#include "stdafx.h"
#include "playlistimpl.h"
#include "system.hpp" 

YSE::playlistImpl::playlistImpl() {
  currentFile = 0;
  nextFile = 0;
  pos.zero();
  spread3D = 0.75f;
  volume = 0.5f;
  speed = 1.0f;
  size = 1.0f;
  relative = true;
  doppler = false;
  pan2D = true;
  stream = true;
  fading = false;
  initFade = false;
  fadeTime = 3000;
  fadeWhenReady = false;
  ch = NULL;
  status = SS_STOPPED;
  chooseSong = true;
  autoloop = true;
}



YSE::playlistImpl::~playlistImpl() {
  if (link) *link = NULL;
}


void YSE::playlistImpl::update() {
  // auto start fade if near end of song
  if (status == SS_PLAYING && autoloop) {
    if (sounds.size() > 1) {
      Flt time = sounds[currentFile].time() / 44100 * 1000;
      Flt trigger = sounds[currentFile].length() / 44100 * 1000 - fadeTime;
      if (time > trigger) {
        initFade = true;
        if (chooseSong) choose();
        chooseSong = true;
      }
    } else if (sounds.size() == 1) {
      if (sounds[currentFile].stopped()) {
        sounds[currentFile].play();
      }
    }
  }

  // start fade if initFade is true
  if (initFade) {
    sounds[currentFile].fadeAndStop(fadeTime);
    sounds[nextFile].volume(volume, fadeTime);
    sounds[nextFile].play();
    currentFile = nextFile;
    initFade = false;
  }
}

void YSE::playlistImpl::choose() {
  if (list.size() < 1) return;
  if (shuffle) {
    nextFile = Random(list.size());
    if (list.size() > 1) {
      while (nextFile == currentFile) nextFile = Random(list.size());
    }
  } else {
    nextFile++;
    if (nextFile >= list.size()) nextFile = 0;
  }
}

void YSE::playlistImpl::play(Int nr) {
  if (sounds.empty()) return;

  if (nr == -1) {
    choose();
    chooseSong = true;
    if (status == SS_PLAYING) {
      initFade = true;
    } else {
      currentFile = nextFile;
      sounds[currentFile].play().volume(volume);
      status = SS_PLAYING;
    }
  } else {
    // specific song
    if (status == SS_PLAYING) {
      nextFile = nr;
      Clamp(nextFile, 0, sounds.size());
      initFade = true;
    } else {
      currentFile = nr;
      Clamp(currentFile, 0, sounds.size());
      sounds[currentFile].play().volume(volume);
      status = SS_PLAYING;
    }
  }
}


