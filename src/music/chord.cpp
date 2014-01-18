#include "stdafx.h"
#include <algorithm>
#include "chord.hpp"
#include "utils/misc.hpp"

namespace YSE {
  namespace MUSIC {

    class chordImpl {
    public:
      std::vector<note> notes;
    };

    chord::chord() {
      pimpl = new chordImpl;
      l = -1;
      v = -1;
    }

    chord::~chord() {
      delete pimpl;
    }

    chord::chord(const chord & value) {
      l = value.l;
      v = value.v;

      FOR(value) {
        pimpl->notes.push_back(value[i]);
      }
    }

    chord& chord::operator()(const chord& value) {
      reset();

      FOR(value) {
        pimpl->notes.push_back(value[i]);
      }
      return *this;
    }

    chord& chord::operator()(const note& value) {
      reset();

      pimpl->notes.push_back(value);
      return *this;
    }

    chord& chord::operator=(const chord& value) {
      reset();

      FOR(value) {
        pimpl->notes.push_back(value[i]);
      }
      return *this;
    }

    chord& chord::operator=(const note& value) {
      reset();

      pimpl->notes.push_back(value);
      return *this;
    }

    chord& chord::operator+=(const chord& value) {
      FOR(value) {
        Bool found = false;
        FORD(j, pimpl->notes) {
          if (pimpl->notes[j].pitch() == value[i].pitch()) {
            found = true;
          }
        }
        if (!found) {
          pimpl->notes.push_back(value[i]);
          if (l != -1) pimpl->notes.back().length(l);
          if (v != -1) pimpl->notes.back().velocity(v);
        }
      }
      return *this;
    }

    chord& chord::operator+=(const note& value) {
      Bool found = false;
      FOR(pimpl->notes) {
        if (pimpl->notes[i].pitch() == value.pitch()) found = true;
      }
      if (!found) {
        pimpl->notes.push_back(value);
        if (l != -1) pimpl->notes.back().length(l);
        if (v != -1) pimpl->notes.back().velocity(v);
      }
      return *this;
    }

    chord& chord::operator-=(const chord& value) {
      FOR(value) {
        for (std::vector<note>::iterator j = pimpl->notes.begin(); j != pimpl->notes.end(); j++) { 
          if ((*j).pitch() == value[i].pitch()) {
            pimpl->notes.erase(j);
            break;
          }
        }
      }
      return *this;
    }

    chord& chord::operator-=(const note& value) {
      for (std::vector<note>::iterator i = pimpl->notes.begin(); i != pimpl->notes.end(); i++) { 
        if ((*i).pitch() == value.pitch()) {
          pimpl->notes.erase(i);
          break;
        }
      }
      return *this;
    }

    chord& chord::reset() {
      l = -1;
      v = -1;
      pimpl->notes.clear();
      return *this;
    }

    UInt chord::size() const {
      return pimpl->notes.size();
    }

    const note & chord::operator[](Int elm) const {
      Clamp(elm, 0, pimpl->notes.size() - 1);
      return pimpl->notes[elm];
    }

    note & chord::operator[](Int elm) {
      Clamp(elm, 0, pimpl->notes.size() - 1);
      return pimpl->notes[elm];
    }

    chord& chord::length(Int value) {
      l = value;
      FOR(pimpl->notes) {
        pimpl->notes[i].length(l);
      }
      return *this;
    }

    Int chord::length() {
      return l;
    }

    chord& chord::velocity(Flt value) {
      v = value;
      FOR(pimpl->notes) {
        pimpl->notes[i].velocity(v);
      }
      return *this;
    }

    Flt chord::velocity() {
      return v;
    }

    bool sortFunc(note & a, note & b) {
      return (a.pitch() < b.pitch());
    }

    chord& chord::sortLowHigh() {
      std::sort(pimpl->notes.begin(), pimpl->notes.end(), sortFunc);
      return *this;
    }

    chord& chord::sortHighLow() {
      sortLowHigh();
      std::reverse(pimpl->notes.begin(), pimpl->notes.end());
      return *this;
    }

    Int chord::contains(const note& value) {
      FOR(pimpl->notes) {
        if (pimpl->notes[i].pitch() == value.pitch() ) {
          return i;
        }
      }
      return -1;
    }



  } // end MUSIC
} // end YSE
