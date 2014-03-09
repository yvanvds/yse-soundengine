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
#include "virtualFinder.h"

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

    /** A job to add to the lowpriority threadpool when tehre are sounds
        to be deleted
    */
    class soundDeleteJob : public ThreadPoolJob {
    public:
      soundDeleteJob() : ThreadPoolJob("soundDeleteJob") {}
      JobStatus runJob();
    };

    /**
      The soundmanager is responsible for the management of all soundfiles and
      sound implementations.
    */
    class soundManager {
    public:
      
      /** Add a new soundfile to the system and return a pointer to it. If the file already
          exists, it will not be loaded anew but a pointer to the existing file will be
          returned. With non-streaming audio it would only consume extra memory if loaded
          multiple times. Of course this is not meant to be used for streaming audio.

          @param file   A reference to the soundfile to add to the system

          @return       A pointer to the soundFile object for this file
      */
      soundFile * addFile(const File & file);
      
      /** Request a new sound implementation.

          &param head   The interface to connect the new implementation to

          @return       A pointer to a new sound implementation
      */
      soundImplementation * addImplementation(sound * head);

      /** This function instructs the soundManager to put the implementation
          in a list of sounds to load. It is called from the sound interface
          create function.
          
          &param impl   The implementation to setup
      */
      void setup(soundImplementation* impl);

      /** Run the soundManager update. This function is responsable for most of the
          action on sound implementations and sound files.
      */
      void update();

      /** Returns true if no sound implementations are active.
      */
      Bool empty();

      /** Sets the maximum amount of sounds to be processed. The soundmanager
          will try to find the sounds that are most relevant and virtualize
          the rest.

          @param value    The desired number of sounds
      */
      void setMaxSounds(Int value);	
      
      /** Get the maximum amount of sounds to be processed.
      */
      Int getMaxSounds();

      /** Retrieve a reader for a file format. This function is used by soundFile
          objects.
      
          @param file   The file to retrieve a reader for.
      */
      AudioFormatReader * getReader(const File & f);     
      
      /** Hints the sounds manager that it should check for implementations that are 
          no longer in use. This is called by the implementations themselves, when they
          find out they're no longer needed.
      */

      Bool inRange(Flt dist);

      void runDeleteJob() { runDelete = true; }

      soundManager();
      ~soundManager();
      juce_DeclareSingleton(soundManager, true)

    private:
      /** the lastGain buffer of each sound is needed to provide smooth changes
      in volume for each channel. When the number of output channels is changed
      this buffer has to change accordingly. This is done by this function.
      */
      void adjustLastGainBuffer();

      // ThreadPoolJobs
      soundSetupJob soundSetup;
      soundDeleteJob soundDelete;

      // a forward list containing all sound files
      std::forward_list<soundFile> soundFiles;

      // a format Manager to assist in reading audio files
      // It is used by soundFiles, but put here because we only need one for all files.
      juce::AudioFormatManager formatManager;

      /* arrays for sound implementations: these are not the soundfiles but the
         implementation side of the interface sound objects.
      */
      std::forward_list<std::atomic<soundImplementation*>> soundsToLoad;
      std::forward_list<soundImplementation*> soundsInUse;
      std::forward_list<soundImplementation> soundImplementations;

      // flag for the update function to check for implementations that should be
      // deleted.
      aBool runDelete;

      virtualFinder vFinder; // for calculating virtual sounds

      // the maximum distance before turning virtual
      // This value is calculated on every update
      aFlt maxDistance;

      friend class soundSetupJob;
      friend class soundDeleteJob;
    };

  }
}




#endif  // SOUNDLOADER_H_INCLUDED
