#pragma once
#include <boost/ptr_container/ptr_vector.hpp>
#include <boost/ptr_container/ptr_list.hpp>
#include <mutex>
#include "headers/types.hpp"
#ifdef WINDOWS
#include <Windows.h>
#endif
#include "channel.hpp"
#include "internal/channelimpl.h"
#include "sound.hpp"
#include "internal/soundimpl.h"
#include "headers/enums.hpp"
#include "internal/playlistimpl.h"
#include "internal/reverbimpl.h"
#include "boost/noncopyable.hpp"
#include "utils/error.hpp"
#include <vector>
#include "instruments/baseInstrumentImpl.h"


namespace YSE {
  /*class mutex : boost::noncopyable {
  public:
    mutex() { handle = CreateMutex(NULL, FALSE, NULL); }
    HANDLE & operator()() { return handle; }
  private:
    HANDLE handle;
  };*/

  class lock : private boost::noncopyable {
  public:
    explicit lock(std::mutex & pm) : mtx(pm) { claim(); }
    ~lock() { mtx.unlock(); }
  private:
    std::mutex & mtx;
    void claim() {
      mtx.lock();
    }
  };


  extern std::mutex MTX;
  extern std::mutex TRACKMTX;
  extern std::mutex RAMPXMTX;
  extern std::mutex SFMTX; // for loading sound files and adding them to a list while no loading is done
  extern std::mutex LoadSoundMutex;

  extern channelimpl * ChannelP; // this is used as an entry point for all recursive channel operations. It points to the global channel implementation.

  void ChangeChannelConf(CHANNEL_TYPE type, Int outputs = 2);

  extern Flt (*occlusionPtr)(const Vec& source, const Vec& listener);

  extern UInt LastBufferSize;

  boost::ptr_list<channelimpl > & Channels    ();
  boost::ptr_list<soundimpl   > & Sounds      ();
  boost::ptr_list<playlistImpl> & Playlists   ();
  boost::ptr_list<reverbimpl  > & Reverbs     ();

  // todo: std::vector<INSTRUMENTS::baseInstrumentImpl*> & Instruments();
}
