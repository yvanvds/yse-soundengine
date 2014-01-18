#include "stdafx.h"
#include "trackImpl.h"
#include "utils/error.hpp"
#include "internal/music/globalTrack.h"

/*#define TARGET_RESOLUTION 1

namespace YSE {
  namespace MUSIC {

    trackImpl::trackImpl(track * value, Int interval) : status(false), link(value), interval(interval), step(0) {
      GlobalTrack().add(this);
    }

    trackImpl::~trackImpl() {
      GlobalTrack().rem(this);
    }

    void trackImpl::update() {
      if (!status) return;
      step++;
      if (step == interval) {
        link->update();
        step = 0;
      }
      FOR(instruments) {
        instruments[i]->update();
      }
    }    

    void trackImpl::allNotesOff() {
      FOR(instruments) instruments[i]->allNotesOff();
    }

  } // end MUSIC
}   // end YSE

*/