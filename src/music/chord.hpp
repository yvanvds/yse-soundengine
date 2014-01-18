#pragma once
#include "note.hpp"
#include <vector>

namespace YSE {
  namespace MUSIC {
    class chordImpl;
    class API chord {
    public: 
      chord();
     ~chord();
      chord(const chord & value);
      chord& operator()(const chord& value);
      chord& operator()(const note & value);
      chord& operator= (const chord& value);
      chord& operator= (const note & value);

      chord& operator+=(const chord& value); // only adds notes that don't exist in the current chord
      chord& operator+=(const note & value); // ...
      chord& operator-=(const chord& value); // remove notes if they exist in the current chord
      chord& operator-=(const note & value); // ...

      chord& length  (Int value); // set length of all notes (if not set, the velocity of the individual notes will be used)
      chord& velocity(Flt value); // set velocity of all notes (if not set, the velocity of the individual notes will be used)
      Int    length  (         );
      Flt    velocity(         );

      chord& reset();
      chord& remove(Int value); // remove note by index
      UInt   size () const;
      const note & operator[](Int elm) const; // elm will be clamped between 0 and vector<note> size - 1
            note & operator[](Int elm)      ;
      note & last () const; // last added element, unless you sorted them
      chord& sortLowHigh(); // sorts according to pitch, from low to high
      chord& sortHighLow(); // sorts according to pitch, from high to low
      Int contains(const note& value); // returns elm with equal pitch, -1 if not found
    private:
      Int l;
      Flt v;
      chordImpl * pimpl;
    };

  } // end MUSIC
}   // end YSE
