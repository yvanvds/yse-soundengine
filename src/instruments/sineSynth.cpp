#include "stdafx.h"
#include "sineSynth.hpp"
#include "internal\instruments\sineSynthImpl.h"
/*
namespace YSE {
  namespace INSTRUMENTS {

    sineSynth::sineSynth() : impl(new sineSynthImpl) {
      // TODO: this is an ugly concept, can't we do anything about it?
      instrument::pimpl = (baseInstrumentImpl*)impl;
    }

    sineSynth::~sineSynth() {
      delete impl;
    }
      
    sineSynth& sineSynth::create(Int voices) {
      impl->create(voices, ChannelGlobal);
      return *this;
    }

    sineSynth& sineSynth::create(Int voices, channel & parent) {
      impl->create(voices, parent);
      return *this;
    }




  } // end INSTRUMENTS
}   // end YSE

*/