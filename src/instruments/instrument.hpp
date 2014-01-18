#pragma once
#include "music/note.hpp"
#include "music/chord.hpp"

/* this is the base class for all instruments.
   It's not meant to be used directly.
*/

namespace YSE {
  namespace MUSIC { class track; } // YSE needs this for internal use

  namespace INSTRUMENTS {
    class baseInstrumentImpl;
    
    class API instrument {
    public:

      void range(Int low,   Int high ); // 2 in one setter
      void low  (Int low ); Int low ();
      void high (Int high); Int high();

      void volume(Flt  value); Flt  volume(); // global instrument volume

      // play or stop notes and chords
      void play(const MUSIC::note & value);
      void play(const MUSIC::chord& value);
      void on  (const MUSIC::note & value);
      void on  (const MUSIC::chord& value);
      void off (const MUSIC::note & value);
      void off (const MUSIC::chord& value);

      // turn all notes off at once
      void allNotesOff();


//======================================================================================
      // parts below are not important for use
      instrument();
     ~instrument();
    protected:
      baseInstrumentImpl * pimpl;
      friend class YSE::MUSIC::track;
    };

  }
}