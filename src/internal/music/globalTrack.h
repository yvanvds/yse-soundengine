#pragma once
#include <vector>
#include "headers/types.hpp"
#ifdef WINDOWS
  #include <Windows.h>
#endif
#include "trackImpl.h"

/*namespace YSE {
  namespace MUSIC {
    extern aUInt latency;

    class globalTrack {
    public:
      globalTrack() : active(false) {}
     ~globalTrack();

      void add(trackImpl * value);
      void rem(trackImpl * value);
      aBool stayAlive;
      aBool signalPause;
      static void CALLBACK update(UINT wTimerID, UINT msg, DWORD dwUser, DWORD dw1, DWORD dw2);
      void stop();

    private:
      void start();
      std::vector<trackImpl*> tracks; 
      Bool active;
      UInt timerRes;

      // timer
      MMRESULT timerID;
      HANDLE htimerQueue;
    };

    globalTrack& GlobalTrack();

  } // end music
}   // end yse
*/