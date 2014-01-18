#include "stdafx.h"
#include "track.hpp"
#include "internal/music/trackImpl.h"
#include "utils/error.hpp"
#include "internal/instruments/samplerImpl.h"
#include "internal/internalObjects.h"
/*
namespace YSE {
  namespace MUSIC {
    track::track(Int interval) : pimpl(new trackImpl(this, interval)) {} 

    track::~track() {
      delete pimpl;
    }

    track & track::addInstrument(INSTRUMENTS::instrument & instrument) {
      lock l(TRACKMTX());
      pimpl->instruments.push_back(instrument.pimpl);
      return *this;
    }

    track & track::remInstrument(INSTRUMENTS::instrument & instrument) {
      lock l(TRACKMTX());
      for(std::vector<INSTRUMENTS::baseInstrumentImpl*>::iterator i = pimpl->instruments.begin();
        i != pimpl->instruments.end(); i++) {

        if (*i == instrument.pimpl) {
          pimpl->instruments.erase(i);
          break;
        }
      }
      return *this;
    }

    track & track::start() {
      pimpl->status = true;
      return * this;
    }

    track & track::stop() {
      pimpl->status = false;
      return * this;
    }

    Bool track::status() {
      return pimpl->status;
    }

  } // end MUSIC
} // end YSE

*/