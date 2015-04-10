/*
  ==============================================================================

    chord.h
    Created: 4 Apr 2014 10:22:53am
    Author:  yvan vander sanden

  ==============================================================================
*/

#ifndef CHORD_H_INCLUDED
#define CHORD_H_INCLUDED

#include "../classes.hpp"
#include "note.hpp"
#include <vector>


namespace YSE {
    
  namespace MUSIC {
        
    class API chord {
    private:
      std::vector<note> notes;
      
    public:
      
      chord();
      chord(const chord & object);
      chord(UInt count, ...);
      
      UInt elms();
      
      chord & transpose(Flt value);
      
      chord & operator+=(const chord& object);
      chord & operator-=(const chord& object);
            
      chord & operator+=(const note& object);
      chord & operator-=(const note& object);
            
      Bool operator==(const chord& object);
      Bool operator!=(const chord& object);
      
            
      friend chord operator+(const chord &c, const note &n);
      friend chord operator-(const chord &c, const note &n);
      
      friend chord operator+(const chord &c1, const chord &c2);
      friend chord operator-(const chord &c1, const chord &c2);
      
      friend chord operator+(const note &n, const chord &c);
      friend chord operator-(const note &n, const chord &c);
    };
        
  }
    
}



#endif  // CHORD_H_INCLUDED
