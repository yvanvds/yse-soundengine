/*
  ==============================================================================

    soundLoader.h
    Created: 28 Jan 2014 11:49:12am
    Author:  yvan

  ==============================================================================
*/

#ifndef SOUNDMANAGER_H_INCLUDED
#define SOUNDMANAGER_H_INCLUDED

#include <forward_list>
#include "JuceHeader.h"
#include "sound.hpp"
#include "soundMessage.h"
#include "soundInterface.hpp"
#include "soundImplementation.h"
#include "../templates/managerObject.h"
#include "../internal/soundFile.h"


// global object for file loading
// used in system.cpp, soundimpl.cpp and soundfile.cpp

namespace YSE {
  namespace SOUND {

    /**
      The soundmanager is responsible for the management of all soundfiles and
      sound implementations.
    */
    class managerObject : public TEMPLATE::managerTemplate<soundSubSystem> {
    public:
      
      virtual void create() {}

      /** Add a new soundfile to the system and return a pointer to it. If the file already
          exists, it will not be loaded anew but a pointer to the existing file will be
          returned. With non-streaming audio it would only consume extra memory if loaded
          multiple times. Of course this is not meant to be used for streaming audio.

          @param file   A reference to the soundfile to add to the system

          @return       A pointer to the soundFile object for this file
      */
      INTERNAL::soundFile * addFile(const File & file);

#if defined PUBLIC_JUCE
      INTERNAL::soundFile * addInputStream(juce::InputStream * source);
#endif

      /** Run the soundManager update. This function is responsable for most of the
          action on sound implementations and sound files.
      */
      void update();

      /** Sets the maximum amount of sounds to be processed. The soundmanager
          will try to find the sounds that are most relevant and virtualize
          the rest.

          @param value    The desired number of sounds
      */
      //void setMaxSounds(Int value);	
      
      /** Get the maximum amount of sounds to be processed.
      */
      //Int getMaxSounds();

      /** Retrieve a reader for a file format. This function is used by soundFile
          objects.
      
          @param file   The file to retrieve a reader for.
      */
      AudioFormatReader * getReader(const File & f);
      AudioFormatReader * getReader(juce::InputStream * source);
      
      
      /** Hints the sounds manager that it should check for implementations that are 
          no longer in use. This is called by the implementations themselves, when they
          find out they're no longer needed.
      */

      //Bool inRange(Flt dist);

      managerObject();
      ~managerObject();
      juce_DeclareSingleton(managerObject, true)

    private:
      /** the lastGain buffer of each sound is needed to provide smooth changes
      in volume for each channel. When the number of output channels is changed
      this buffer has to change accordingly. This is done by this function.
      */
      void adjustLastGainBuffer();

      // a forward list containing all sound files
      std::forward_list<INTERNAL::soundFile> soundFiles;

      // a format Manager to assist in reading audio files
      // It is used by soundFiles, but put here because we only need one for all files.
      juce::AudioFormatManager formatManager;

      // the maximum distance before turning virtual
      // This value is calculated on every update
      //aFlt maxDistance;
    };

  }
}




#endif  // SOUNDLOADER_H_INCLUDED
