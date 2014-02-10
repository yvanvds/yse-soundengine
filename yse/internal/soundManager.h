/*
  ==============================================================================

    soundLoader.h
    Created: 28 Jan 2014 11:49:12am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDLOADER_H_INCLUDED
#define SOUNDLOADER_H_INCLUDED

#include "soundFile.h"
#include <deque>
#include <forward_list>
#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "JuceHeader.h"
#include "../classes.hpp"

// global object for file loading
// used in system.cpp, soundimpl.cpp and soundfile.cpp

namespace YSE {
  namespace INTERNAL {


    class soundManager : public Thread {
    public:
      /** Add a new soundfile to the system and return a pointer to it. If the file already
      exists, it will not be loaded anew but a pointer to the existing file will be
      returned. With non-streaming audio it would only consume extra memory if loaded
      multiple times. Of course this is not meant to be used for streaming audio.
      */
      soundFile * add(const File & file);

      /** Manually remove a soundfile from the system.
      */
      //void remove(std::forward_list<soundFile>::iterator & file);

      /** Add a soundFile to the loader. Files are not loaded instantly but rather when
      there is time to spare. (This will be almost instantly but ensures that current
      audio operations are not disrupted.
      */
      void addToQue(soundFile * elm);

      /** Returns true if no sound objects are active.
      */
      Bool empty();
      void maxSounds(Int value);	Int maxSounds(); // sets nonvirtual sounds

      void adjustLastGainBuffer();

      void update();

      //std::forward_list<soundImplementation> & soundObjectList();
      soundImplementation * addImplementation();
      void removeImplementation(soundImplementation * ptr);

      void run();

      soundManager();
      ~soundManager();
      juce_DeclareSingleton(soundManager, true)

    private:
      CriticalSection readQue;
      std::deque<soundFile *> soundFilesQue; // soundFiles that need loading are placed in here
      std::forward_list<soundFile> soundFiles;

      juce::AudioFormatManager formatManager;
      ScopedPointer<AudioFormatReaderSource> currentAudioFileSource;

      /* array for sound implementations: these are not the soundfiles but the
         implementation side of the interface sound objects.
         */
      std::forward_list<soundImplementation> soundObjects;
      Int nonVirtualSize;
    };

  }
}




#endif  // SOUNDLOADER_H_INCLUDED
