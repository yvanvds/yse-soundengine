#include "stdafx.h"
#include "globalTrack.h"

#include "internal/internalObjects.h"

/*
#define TARGET_RESOLUTION 1

namespace YSE {
  namespace MUSIC {
    aUInt latency = 0;

    globalTrack& GlobalTrack() {
      static globalTrack gt;
      return gt;
    }

    void globalTrack::add(trackImpl * value) {
      lock l(TRACKMTX());
      tracks.push_back(value);
      if (tracks.size()) start();
    }

    void globalTrack::rem(trackImpl * value) {
      lock l(TRACKMTX());
      if (tracks.size()) {
        for (std::vector<trackImpl*>::iterator i = tracks.begin(); i != tracks.end(); ) {
          if (*i == value) {
            (*i)->allNotesOff();
            i = tracks.erase(i);
            
          } else i++;
        }
      }
      if (!tracks.size()) stop();
    }

    void CALLBACK globalTrack::update(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dw1, DWORD dw2) {
      lock l(TRACKMTX());
      globalTrack * obj = (globalTrack*)dwUser;
      FOR(obj->tracks) {
        obj->tracks[i]->update();
      }
      latency++;
    }

    void globalTrack::start() {
      if (active) return;

      TIMECAPS tc;
      timeGetDevCaps(&tc, sizeof(TIMECAPS));
      timerRes = min(max(tc.wPeriodMin, TARGET_RESOLUTION), tc.wPeriodMax);
      timeBeginPeriod(timerRes);

      timerID = timeSetEvent(1, timerRes, update, (DWORD)this, TIME_PERIODIC | TIME_KILL_SYNCHRONOUS);
      active = true;
    }

    void globalTrack::stop() {
      if (!active) return;
      if (timeKillEvent(timerID) != TIMERR_NOERROR) {
        Error.emit(E_TRACK_TIMER_STOP);
      }
      timeEndPeriod(timerRes);

      active = false;
    }

    globalTrack::~globalTrack() {
      stop();
    }

  }
}

*/