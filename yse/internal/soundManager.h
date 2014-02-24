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
#include <queue>
#include "../headers/defines.hpp"
#include "../headers/types.hpp"
#include "JuceHeader.h"
#include "../classes.hpp"
#include "../sound.hpp"

// global object for file loading
// used in system.cpp, soundimpl.cpp and soundfile.cpp

namespace YSE {
  namespace INTERNAL {
    /** A job to add to the lowpriority threadpool when there are sounds
        that need to be setup
    */
    class soundSetupJob : public ThreadPoolJob {
    public:
      soundSetupJob() : ThreadPoolJob("soundSetupJob") {}
      JobStatus runJob();
    };

    class soundDeleteJob : public ThreadPoolJob {
    public:
      soundDeleteJob() : ThreadPoolJob("soundDeleteJob") {}
      JobStatus runJob();
    };

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

      soundImplementation * addImplementation(sound * head);
      void loadImplementation(soundImplementation* impl);
      void runDeleteJob() { runDelete = true; }

      void run();

      soundManager();
      ~soundManager();
      juce_DeclareSingleton(soundManager, true)

    private:
      soundSetupJob soundSetup;
      soundDeleteJob soundDelete;

      CriticalSection readQue;
      std::deque<soundFile *> soundFilesQue; // soundFiles that need loading are placed in here
      std::forward_list<soundFile> soundFiles;

      juce::AudioFormatManager formatManager;
      ScopedPointer<AudioFormatReaderSource> currentAudioFileSource;

      /* arrays for sound implementations: these are not the soundfiles but the
         implementation side of the interface sound objects.

         */
      std::forward_list<std::atomic<soundImplementation*>> soundsToLoad;
      std::forward_list<soundImplementation*> soundsInUse;
      std::forward_list<soundImplementation> soundImplementations;
      aBool runDelete;

      Int nonVirtualSize;

      friend class soundSetupJob;
      friend class soundDeleteJob;
    };

  }
}




#endif  // SOUNDLOADER_H_INCLUDED
