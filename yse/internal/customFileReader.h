/*
  ==============================================================================

    customFileReader.h
    Created: 23 Apr 2014 5:22:57pm
    Author:  yvan

  ==============================================================================
*/

#ifndef CUSTOMFILEREADER_H_INCLUDED
#define CUSTOMFILEREADER_H_INCLUDED
#include "JuceHeader.h"
#include "../classes.hpp"
#include "../headers/types.hpp"

namespace YSE {
  namespace INTERNAL {
    class customFileReader {
    public:
      customFileReader();
     ~customFileReader();
      
      Bool create(const char * filename);

      I64 getTotalLength();
      Bool isExhausted();
      Int read(void *destBuffer, Int maxBytesToRead);
      I64 getPosition();
      Bool setPosition(I64 newPosition);

    private:
      void * fileHandle;
      I64 fileSize;
    };

    namespace CALLBACK {
      extern Bool(*openPtr)(const char * filename, I64 * filesize, void ** fileHandle);
      extern void(*closePtr)(void * fileHandle);
      extern Bool(*endOfFilePtr)(void * fileHandle);
      extern Int(*readPtr)(void * destBuffer, Int maxBytesToRead, void * fileHandle);
      extern I64(*getPosPtr)(void * fileHandle);
      extern Bool(*setPosPtr)(I64 newPosition, void * fileHandle);
      extern Bool(*fileExists)(const char * filename);
    }
    
  }
}



#endif  // CUSTOMFILEREADER_H_INCLUDED
