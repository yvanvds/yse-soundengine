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

    io& open       (bool      (*funcPtr)(const char * filename, long long * filesize,     void ** fileHandle));
    io& close      (void      (*funcPtr)(                                                 void *  fileHandle));
    io& read       (long long (*funcPtr)(void * destBuffer    , long long maxBytesToRead, void *  fileHandle));
    io& getPosition(long long (*funcPtr)(                                                 void *  fileHandle));
    io& fileExists (bool      (*funcPtr)(const char * filename));
    io& length     (long long (*funcPtr)(                                                  void *  fileHandle));
    io& seek       (long long (*funcPtr)(long long offset     , int whence,                void *  fileHandle));

    io&  setActive(bool value);
    Bool getActive(          );
    
  private:
    aBool active;
  };

  API io & IO();
}



#endif  // IO_H_INCLUDED
