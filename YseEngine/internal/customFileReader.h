/*
  ==============================================================================

    customFileReader.h
    Created: 23 Apr 2014 5:22:57pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CUSTOMFILEREADER_H_INCLUDED
#define CUSTOMFILEREADER_H_INCLUDED
#include "../classes.hpp"
#include "../headers/types.hpp"

#include <sndfile.hh>

namespace YSE {
  namespace INTERNAL {
    class customFileReader {
    public:
      customFileReader();
     ~customFileReader();
      
      static bool Open(const char * filename, long long * filesize, void ** fileHandle);
      static void Close(void * fileHandle);

      static void UpdateVIO();
      static void ResetVIO();
      static SF_VIRTUAL_IO & GetVIO();
    };

    namespace CALLBACK {
      extern bool(*openPtr)(const char * filename, long long * filesize, void ** fileHandle);
      extern void(*closePtr)(void * fileHandle);
      extern long long(*readPtr)(void * destBuffer, long long maxBytesToRead, void * fileHandle);
      extern long long(*getPosPtr)(void * fileHandle);
      extern bool(*fileExists)(const char * filename);
      extern long long(*lengthPtr)(void * fileHandle);
      extern long long(*seekPtr)(long long offset, int whence, void * fileHandle);
    }
    
  }
}



#endif  // CUSTOMFILEREADER_H_INCLUDED
