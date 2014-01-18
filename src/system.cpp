#include "stdafx.h"
#include <boost/ptr_container/ptr_vector.hpp>
#include <thread>

#include "system.hpp"
#include "backend/ysetime.h"
#include "backend/reverbbackend.h"
#include "backend/soundLoader.h"
#include "backend/soundfile.h"
#include "internal/listenerimpl.h"
#include "internal/settings.h"
#include "internal/channelimpl.h"
#include "internal/soundimpl.h"
#include "internal/playlistimpl.h"
#include "internal/internalObjects.h"
#include "internal/music/globalTrack.h"
#include "utils/error.hpp"
#include "utils/guard.h"
#include "reverb.hpp"

#if defined(USE_PORTAUDIO)
  #include "backend/pa.h"
#elif defined(USE_OPENSL)
  #include "backend/opensl.h"
#endif // defined

YSE::system YSE::System;

struct systemImpl {
	YSE::reverbimpl finalReverb;
	Flt totalAdjust;
	boost::ptr_vector<YSE::soundimpl*> nonVirtual;
	Int low;
	void findLow();
	YSE::systemDevice _driver;
	Bool _inCave;
  std::thread fileLoader;
} SI;

Bool YSE::system::init() {
  if (SI._driver.initialize()) {
	  // add global output channel
    ChannelGlobal.createGlobal();
    ChannelGlobal.pimpl->userChannel = false;
    ChannelP = ChannelGlobal.pimpl;

    ChannelAmbient.create();
    ChannelAmbient.pimpl->userChannel = false;

    ChannelFX.create();
    ChannelFX.pimpl->userChannel = false;

    ChannelMusic.create();
    ChannelMusic.pimpl->userChannel = false;

    ChannelGui.create();
    ChannelGui.pimpl->userChannel = false;

    ChannelVoice.create();
    ChannelVoice.pimpl->userChannel = false;

	  GlobalReverb.create();
	  GlobalReverb.preset(REVERB_GENERIC);
	  GlobalReverb.active(false);
	  GlobalReverb.pos(Vec(0));
	  GlobalReverb.size(10);
	  GlobalReverb.rolloff(0);
	  GlobalReverb.pimpl->global = true;
	  ChannelGlobal.pimpl->reverb = &YSE::ReverbBackend;

	  maxSounds(50);

    // set default device
    setDevice(activeDevice(), YSE::CT_AUTO);


    // create thread to start loading soundfiles
    KeepLoading = true;
    SI.fileLoader = std::thread(LoadSoundFiles);
    return true;
  }
  return false;
}

void YSE::system::update() {
  lock l(MTX);

  // load more sound files
  if (YSE::LoadList().size()) LoadSoundCondition.notify_all();

	Time.update();
	ListenerImpl.update();

  // release old sounds
  {
    boost::ptr_list<soundimpl>::iterator i = Sounds().begin();
    while (i != Sounds().end()) {
      if (i->_release && i->_status == SS_STOPPED) {
        if (i->file != NULL) i->file->release();
        if (i->parent != NULL) i->parent->remove(&*i);
        i = Sounds().erase(i);
      } else ++i;
    }
  }

  // update instruments
  /* Todo: std::vector<INSTRUMENTS::baseInstrumentImpl*>::iterator i = Instruments().begin();
  while (i != Instruments().end()) {
    (*i)->updateVoices();
    ++i;
  }
  // Todo: MUSIC::latency = 0; */

  // update or remove channels if needed
  {
    boost::ptr_list<channelimpl>::iterator i = Channels().begin();
    while (i != Channels().end()) {
      if (i->_release) {

        // move subchannels to parent
        for (std::vector<channelimpl*>::iterator j = i->children.begin(); j != i->children.end(); ++j) {
          (*j)->parent = i->parent;
          i->parent->children.push_back(*j);
        }

        // move sounds to parent
        for (std::vector<soundimpl*>::iterator j = i->sounds.begin(); j != i->sounds.end(); ++j) {
          (*j)->parent = i->parent;
          i->parent->sounds.push_back(*j);
        }

        i = Channels().erase(i);
      } else {
        i->update();
        ++i;
      }
    }
  }



  // update playlists
  for (boost::ptr_list<playlistImpl>::iterator i = Playlists().begin(); i != Playlists().end(); ++i) {
    i->update();
  }

	// update sounds
	for (boost::ptr_list<soundimpl>::iterator i = Sounds().begin();  i != Sounds().end(); ++i) {
		i->update();
	}

	// calculate virtual sounds
	Int index = 0;
  for (boost::ptr_list<soundimpl>::iterator i = Sounds().begin();  i != Sounds().end(); ++i) {
		// skip sound that don't play
		if (i->_loading) continue;
		if (i->_status == YSE::SS_STOPPED) continue;
		if (i->_status == YSE::SS_PAUSED) continue;

		// fill buffer while not full
		if (index < SI.nonVirtual.size()) {
			SI.nonVirtual[index] = &*i;
			index++;
			if (index == SI.nonVirtual.size()) SI.findLow();
		} else {
			if ((!i->parent->allowVirtual()) || (i->virtualDist < SI.nonVirtual[SI.low]->virtualDist)) {
				// if low == -1 no sounds are allowed to go virtual, so we can't add any more sounds
				// sounds that are not allowed to go virtual are played anyway because isVirtual is
				// set to false in the update loop

				if (SI.low > -1) {
					SI.nonVirtual[SI.low] = &*i;
					SI.findLow();
				}
			}
		}
	}

	for (Int i = 0; i < index; i++) {
		SI.nonVirtual[i]->isVirtual = false;
	}

	// calculate reverb
	// clear current final reverb
	SI.finalReverb.preset(REVERB_OFF);
  SI.finalReverb.active(GlobalReverb.active());
	SI.finalReverb.dry(0.0);
	SI.totalAdjust = 0;

  // release stale reverbs
  {
    boost::ptr_list<reverbimpl>::iterator i = Reverbs().begin();
    while (i != Reverbs().end()) {
      if (i->_release) {
        i = Reverbs().erase(i);
      } else ++i;
    }
  }

	// find reverbs within distance first
	for (boost::ptr_list<reverbimpl>::iterator i = Reverbs().begin(); i != Reverbs().end(); ++i) {
		if (i->global) continue; // don't include global reverb right now
		if (!i->_active) continue;
		if (Dist(i->_position, ListenerImpl._newPos) <= i->_size) {
			// add this reverb
      SI.finalReverb += *i;
			SI.totalAdjust += 1;
      SI.finalReverb._active = true;
		}
	}

	// if reverb within distance has been found use this
	if (SI.totalAdjust > 1) {
		SI.finalReverb /= SI.totalAdjust;
	}

	// else look for rolloff's
	if (SI.totalAdjust == 0) {
		// check again, for partial reverbs
		for (boost::ptr_list<reverbimpl>::iterator i = Reverbs().begin(); i != Reverbs().end(); ++i) {
			if (i->global) continue;
			if (!i->_active) continue;
			if (Dist(i->_position, ListenerImpl._newPos) <= i->_size + i->_rolloff) {
				// add partial reverb
				Flt adjust = 1 - (Dist(i->_position, ListenerImpl._newPos) - i->_size) / i->_rolloff;
				SI.finalReverb += *i * adjust;
				SI.totalAdjust += adjust;
        SI.finalReverb._active = true;
			}
		}
	}

	if (SI.totalAdjust > 1) {
		SI.finalReverb /= SI.totalAdjust;
	} else if (SI.totalAdjust < 1) {
		// add global reverb as much as need if it is active
		if (GlobalReverb.active()) {
			SI.finalReverb += *GlobalReverb.pimpl * (1 - SI.totalAdjust);
		}
		if (SI.totalAdjust > 0) SI.finalReverb._active = true;
	}
	ReverbBackend.set(SI.finalReverb);

  // check for idle soundfiles in memory
  {
    boost::ptr_map<std::string, soundFile>::iterator i = SoundFiles.begin();
    while (i != SoundFiles.end()) {
      if (!i->second->active()) {
        i = SoundFiles.erase(i);
      } else ++i;
    }
  }

  //GuardControl().up();
}

void YSE::system::close() {
  KeepLoading = false; // global var checked by file loading routine
  // Todo: MUSIC::GlobalTrack().stop();
  LoadSoundCondition.notify_all(); //resume file loading so it will discover keeploading is false and shut itself down
  SI.fileLoader.join(); // wait for thread to finish
	SI._driver.close();   // close audio driver
}

Int YSE::system::numDevices() {
	return SI._driver.devices.size();
}

Int YSE::system::activeDevice() {
	return SI._driver.activeDevice;
}

Bool YSE::system::setDevice(UInt ID, CHANNEL_TYPE conf, Int count) {
	if (ID >= SI._driver.devices.size()) return false;

	if (conf == CT_AUTO) ChangeChannelConf(conf, SI._driver.devices[ID].outChannels);
	else if (conf == CT_CUSTOM) ChangeChannelConf(conf, count);
	else ChangeChannelConf(conf);

	AdjustLastGainBuffer();
	if (SI._driver.devices[ID].outChannels < ChannelP->out.size()) return false;
	SI._driver.openDevice(ID, ChannelP->out.size());

  // adjust reverb channels
  ReverbBackend.channels(count);
	return true;
}

YSE::system& YSE::system::dopplerScale(Flt scale) {
	Settings.dopplerScale = scale;
	return (*this);
}

Flt YSE::system::dopplerScale() {
	return Settings.dopplerScale;
}

YSE::system& YSE::system::distanceFactor(Flt factor) {
	Settings.distanceFactor = factor;
	return (*this);
}

Flt YSE::system::distanceFactor() {
	return Settings.distanceFactor;
}

YSE::system& YSE::system::rolloffScale(Flt scale) {
	Settings.rolloffScale = scale;
	return (*this);
}

Flt YSE::system::rolloffScale() {
	return Settings.rolloffScale;
}

YSE::system& YSE::system::maxSounds(Int value) {
  lock l(MTX);

	while (SI.nonVirtual.size() < value) SI.nonVirtual.push_back(new soundimpl*);
	while (SI.nonVirtual.size() > value) SI.nonVirtual.pop_back();
	return (*this);
}

Int YSE::system::maxSounds() {
	return SI.nonVirtual.size();
}

YSE::system::~system() {
}

void systemImpl::findLow() {
	// call this only when buffer is completely filled !!!
	low = 0;
	for (Int i = 1; i < nonVirtual.size(); i++) {
		if (!nonVirtual[i]->parent->allowVirtual()) continue;
		if (nonVirtual[i]->virtualDist > nonVirtual[low]->virtualDist) low = i;
	}
	if (!nonVirtual[low]->parent->allowVirtual()) low = -1;
}

Flt YSE::system::cpuLoad() {
	return SI._driver.cpuLoad();
}

void YSE::system::sleep(UInt ms) {
  SI._driver.sleep(ms);
}

YSE::system& YSE::system::setOcclusionCallback(Flt(*func)(const YSE::Vec&, const YSE::Vec&)) {
  occlusionPtr = func;
  return (*this);
}

YSE::audioDevice & YSE::system::getDevice(UInt nr) {
  if (nr >= SI._driver.deviceList.size()) nr = SI._driver.deviceList.size() - 1;
  return SI._driver.deviceList[nr];
}
