#include "stdafx.h"
#include "sampler.hpp"
#include "internal/instruments/samplerImpl.h"
/*
namespace YSE {
  namespace INSTRUMENTS {

    sampler::sampler() : impl(new samplerImpl) {
      // TODO: this is an ugly concept, can't we do anything about it?
      instrument::pimpl = (baseInstrumentImpl*)impl;
    }

    sampler::~sampler() {
      delete impl;
    }
      
    sampler& sampler::create(const char * fileName, Int voices, Int pitch) {
      impl->create(fileName, voices, pitch, ChannelGlobal);
      return *this;
    }

    sampler& sampler::create(const char * fileName, Int voices, Int pitch, channel & parent) {
      impl->create(fileName, voices, pitch, parent);
      return *this;
    }

  

    sampler& sampler::loop(Bool value) {
      impl->loop = value;
      FOR(impl->voices) {
        impl->voices[i].loop(value);
      }
      return *this;
    }

    Bool sampler::loop() {
      return impl->loop;
    }



  } // end INSTRUMENTS
}   // end YSE

*/