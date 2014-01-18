#pragma once
#include <vector>
#include <string>
#include "utils/vector.hpp"
#include "sound.hpp"
#include "internal/soundimpl.h"
#include <boost/ptr_container/ptr_vector.hpp>

namespace YSE {
  class playlistImpl {
  public:
    playlistImpl();
   ~playlistImpl();

    void update();
    void play(Int nr = -1);
    void choose();

    std::vector<std::string> list;
    boost::ptr_vector<sound> sounds;
    Int currentFile;
    Int nextFile;
    Vec pos;
    Flt spread3D;
    Flt volume;
    Flt speed;
    Flt size;
    Bool relative;
    Bool doppler;
    Bool pan2D;
    Bool stream;

    // for fader process
    UInt fadeTime;
    Bool fading;
    Bool initFade;
    Bool shuffle;
    Bool fadeWhenReady;
    Bool chooseSong;
    Bool autoloop;

    SOUND_STATUS status;
    channel * ch;

    playlistImpl ** link;
  };
}