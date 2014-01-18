#include "stdafx.h"
#include "internalObjects.h"

// used for calculating track latency
UInt YSE::LastBufferSize = 0;

YSE::channelimpl * YSE::ChannelP;
std::mutex YSE::MTX;
std::mutex YSE::TRACKMTX;
std::mutex YSE::RAMPXMTX;
std::mutex YSE::SFMTX;
std::mutex YSE::LoadSoundMutex;


Flt (*YSE::occlusionPtr)(const YSE::Vec& source, const YSE::Vec& listener) = NULL;

boost::ptr_list<YSE::playlistImpl> & YSE::Playlists() {
  static boost::ptr_list<playlistImpl> list;
  return list;
}

boost::ptr_list<YSE::channelimpl> & YSE::Channels() {
  static boost::ptr_list<channelimpl> list;
  return list;
}

boost::ptr_list<YSE::soundimpl> & YSE::Sounds() {
  static boost::ptr_list<soundimpl> list;
  return list;
}

boost::ptr_list<YSE::reverbimpl> & YSE::Reverbs() {
  static boost::ptr_list<reverbimpl> list;
  return list;
}

/* Todo
std::vector<YSE::INSTRUMENTS::baseInstrumentImpl*> & YSE::Instruments() {
  static std::vector<INSTRUMENTS::baseInstrumentImpl*> list;
  return list;
}*/
