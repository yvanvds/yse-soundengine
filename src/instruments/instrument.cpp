#include "stdafx.h"
#include "instrument.hpp"
#include "internal\instruments\baseInstrumentImpl.h"

/*
namespace YSE {
  namespace INSTRUMENTS {

    instrument::instrument() {}

    instrument::~instrument() {
      // don't delete pimpl in this case because it's only a pointer to the base type of the
      // real implementation, which is implemented in the derived class
    }
      

    void instrument::range(Int low, Int high) {
      pimpl->lowestNote = low;
      pimpl->highestNote = high;
    }

    void instrument::low(Int low) {
      pimpl->lowestNote = low;
    }

    Int instrument::low() {
      return pimpl->lowestNote;
    }

    void instrument::high(Int high) {
      pimpl->highestNote = high;
    }

    Int instrument::high() {
      return pimpl->highestNote;
    }

    void instrument::volume(Flt value) {
      pimpl->ch.volume(value);
    }

    void instrument::play(const MUSIC::note & value) {
      pimpl->play(value.pitch(), value.velocity(), value.length());
    }

    void instrument::play(const MUSIC::chord & value) {
      FOR(value) {
        pimpl->play(value[i].pitch(), value[i].velocity(), value[i].length());
      }
    }

    void instrument::on(const MUSIC::note & value) {
      pimpl->play(value.pitch(), value.velocity(), -1);
    }

    void instrument::on(const MUSIC::chord & value) {
      FOR(value) {
        pimpl->play(value[i].pitch(), value[i].velocity(), -1);
      }
    }

    void instrument::off(const MUSIC::note & value) {
      pimpl->stop(value.pitch());
    }

    void instrument::off(const MUSIC::chord & value) {
      FOR(value) {
        pimpl->stop(value[i].pitch());
      }
    }

    void instrument::allNotesOff() {
      pimpl->allNotesOff();
    }


  } // end INSTRUMENTS
}   // end YSE

*/