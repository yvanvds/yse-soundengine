/*
  ==============================================================================

    io.h
    Created: 23 Apr 2014 7:33:47pm
    Author:  yvan

  ==============================================================================
*/

#ifndef IO_H_INCLUDED
#define IO_H_INCLUDED

#include "headers/types.hpp"

namespace YSE {
  /* 
  */
  class API io {
  public:
    io(); // There is a global functor for this object. Don't create your own.

    io& open       (Bool(*funcPtr)(const char * filename, I64 * filesize, void ** fileHandle));
    io& close      (void(*funcPtr)(                                       void *  fileHandle));
    io& endOfFile  (Bool(*funcPtr)(                                       void *  fileHandle));
    io& read       (Int (*funcPtr)(void * destBuffer, Int maxBytesToRead, void *  fileHandle));
    io& getPosition(I64 (*funcPtr)(                                       void *  fileHandle));
    io& setPosition(Bool(*funcPtr)(I64 newPosition                      , void *  fileHandle));
    io& fileExists(Bool(*funcPtr)(const char * filename));

    io&  setActive(Bool value);
    Bool getActive(          );
    
  private:
    aBool active;
  };

  API io & IO();
}



#endif  // IO_H_INCLUDED
