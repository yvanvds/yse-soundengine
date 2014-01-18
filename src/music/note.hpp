#pragma once

namespace YSE {
  namespace MUSIC {

    class API note {
    public:
      note(Flt pitch = 0, Flt velocity = 0.0f, Int length = 0) : p(pitch), v(velocity), l(length) {}

      note& operator()(Flt pitch, Flt velocity = 0.5f, Int length = 0) { p = pitch; v = velocity; l = length; return *this;}

      note& pitch   (Flt  value)       { p = value; return *this; } 
      note& length  (Int  value)       { l = value; return *this; } 
      note& velocity(Flt  value)       { v = value; return *this; } 
      Flt   pitch   (          ) const {            return p    ; }
      Int   length  (          ) const {            return l    ; }
      Flt   velocity(          ) const {            return v    ; }

      //operator Flt() const { return      p; } // returns pitch

      void  operator++(         )       { ++p; }
      void  operator++(Int      )       { p++; }
      void  operator--(         )       { --p; }
      void  operator--(Int      )       { p--; }
      note& operator+=(Flt value)       { p += value; return *this; }
      note& operator-=(Flt value)       { p -= value; return *this; }
      Bool  operator==(Flt value) const { return p == value;        }
      Bool  operator!=(Flt value) const { return p != value;        }
      Bool  operator< (Flt value) const { return p <  value;        }
      Bool  operator> (Flt value) const { return p >  value;        }
      note& operator= (Flt value)       { p = value;  return *this; }

      note& zero() { p = 0; v = 0; l = 0; return *this; }

    private:
      Int l;
      Flt p, v;
    };

  } // end MUSIC
}   // end YSE
      